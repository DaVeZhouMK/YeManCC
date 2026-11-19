"""Read-only exported-log audit. Reports evidence, never executes product code.

Current source policy is advisory: an EXE hash is not comparable to a source hash.
Volume alone is not a lane violation. Unrecognised streams/events remain unaudited.
"""
from __future__ import annotations

import argparse
import collections
from contextlib import ExitStack
import datetime
import hashlib
import io
import json
import math
import os
import re
import zipfile
from pathlib import Path
from ai_log_reader import iter_log_members, canonical_name, ZIP_READ_ERRORS, evidence_json_loads, COST_ARCHIVE, json_rejection_evidence, evidence_error_type
from steam_tail_alias_reader import PAIRS as STEAM_TAIL_PAIRS, TAIL_LIMIT, alias_claims

NATIVE_DETAIL = {
    'rog.hid-discovery', 'input-capture-bus-rate-sample', 'sleep-device-tree-sample',
    'gamepad-xinput-input', 'gamepad-ui-input-emitted', 'system-xinput-heartbeat',
    'onex-back-buttons', 'onex-oem-input-edge', 'rog-oem-key', 'rog-oem-key-unknown',
    'legion-back-buttons', 'legion-s-back-buttons', 'gpd-back-buttons',
    'input-idle-window', 'input-capture-tick-cost', 'input-capture-ui-cost',
    'monitor-data-snapshot', 'input-host-scheduling-delay',
}
LANES = {
    'virtual-adaptation-session.jsonl': 'virtual-adaptation-process-export', 'model-adaptation-session.jsonl': 'model-adaptation-process-export', 'model-adaptation-signals-session.jsonl': 'model-signal-process-export', 'model-adaptation.log': 'adaptation-low', 'virtual-adaptation.log': 'virtual-adaptation-bounded', 'app.log': 'app-low', 'native-lifecycle.log': 'native-low',
    'native-detail.log': 'native-detail', 'monitor-detail.log': 'monitor-detail', 'fan-lifecycle.log': 'fan-low',
    'fan-api.log': 'fan-detail', 'virtual-gamepad.log': 'gamepad-detail',
    'gyro-motion.log': 'gyro-detail', 'gyro-motion-samples.log': 'gyro-samples', 'input-capture.jsonl': 'input-detail',
    'tdp.log': 'tdp-low', 'tdp-detail.log': 'tdp-detail',
    'sleep-facts.log': 'sleep-low', 'steam-original-recovery.jsonl': 'sleep-recovery-low', 'frontend-trace.log': 'frontend-low',
    'frontend-trace-detail.log': 'frontend-detail',
    'frontend-errors.log': 'frontend-low', 'webview-failures.log': 'frontend-low',
    'input-host-cost.log': 'host-detail', 'input-host-xbox-sdk.log': 'host-detail',
    'input-host-signal.log': 'host-detail', 'input-host-report-chain.log': 'host-detail',
    'input-host-fatal.log': 'host-low', 'input-host-rejects.log': 'host-low',
    'recovery-service.log': 'recovery-low',
    'battery-charge-client.log': 'battery-low', 'battery-charge-host.log': 'battery-low',
    'focus-trace.log': 'focus-detail', 'hardware-coordinator.log': 'fan-detail',
    'decky-sidebar.log': 'decky-mixed',
    'yeman-fan-host-runtime.log': 'fan-host-mixed', 'fan-host-fault.log': 'fan-host-fault',
}
TRANSPORT_FIELDS = {'time', 'timestamp', 'timestampUtc', 'timestampLocal', 'ts', 'pid',
                    'processId', 'runId', 'hostInstanceId', 'hwnd',
                    'powerGeneration', 'powerLifecycle', 'writerPid', 'factSchemaVersion',
                    'writerThreadId', 'writerProcessCreation100ns', 'writerInstanceId', 'writerIdentityStatus',
                    'writerPowerGeneration', 'observedTickMs', 'writerSequence', 'utcEpochMs', 'utcOffsetMinutes',
                    'clockScope', 'observationScope'}


def canonical_log_name(name):
    # Both native suffix rotations and FanHost extension-preserving rotations.
    return canonical_name(name)


HOST_SIGNAL_WRITER_MAX_INSTANCES = 32
HOST_SIGNAL_WRITER_COUNTERS = ('offered', 'accepted', 'rejected', 'invalid', 'written',
                              'failed', 'flagDisabled', 'unfinished', 'admissionInProgress', 'discardedOnClose')
HOST_SIGNAL_SHARED_COUNTERS = ('Accepted', 'Dropped', 'Completed', 'Failed', 'Discarded',
                              'DroppedBusy', 'DroppedFull', 'DroppedClosed')


def host_signal_writer_evidence(filename, rows):
    if not re.fullmatch(r'input-host-(?:cost|signal)\.log(?:\.(?:prev|previous|[0-9]+))?', filename):
        return None
    cost = canonical_log_name(filename) == 'input-host-cost.log'
    writer_versions = (1, 2) if cost else (1,)
    statuses, groups = collections.Counter(), {}

    def counter(value):
        return value if type(value) is int and 0 <= value < (1 << 63) else None

    for line, row in rows:
        if cost:
            if not all(counter(row.get(key)) is not None for key in ('windowMs', 'frames', 'writerPid')) or not row['writerPid']:
                continue
            writer, identity, kind = row.get('signalLogWriter'), row, 'cost-window'
        else:
            kind = row.get('kind')
            if kind not in ('host-virtual-signal', 'host-virtual-signal-tail'):
                continue
            if type(row.get('schema')) is not int or row['schema'] != 1:
                statuses['unsupported-signal-schema'] += 1
                continue
            if row.get('deferredQueueAdmission') is not True:
                statuses['deferred-writer-not-observed'] += 1
                continue
            writer, identity = row.get('logWriter'), row.get('request')
        if not isinstance(writer, dict):
            statuses['writer-not-observed' if writer is None else 'invalid-writer'] += 1
            continue
        if type(writer.get('version')) is not int or writer['version'] not in writer_versions or writer.get('snapshotAtomic') is not False:
            statuses['unsupported-writer-schema'] += 1
            continue
        instance = identity.get('hostInstanceId') if isinstance(identity, dict) else None
        if (not isinstance(instance, str) or re.fullmatch(r'[0-9a-f]{32}', instance) is None or
                (not cost and identity.get('bindingTextTruncated') is not False)):
            statuses['unbound-host-instance'] += 1
            continue
        if instance not in groups and len(groups) == HOST_SIGNAL_WRITER_MAX_INSTANCES:
            statuses['instance-capacity-omitted'] += 1
            continue
        statuses['projected'] += 1
        group = groups.setdefault(instance, {'kinds': collections.Counter(), 'witnesses': {}})
        group['kinds'][kind] += 1
        signal = {key: counter(writer.get(key)) for key in HOST_SIGNAL_WRITER_COUNTERS}
        stopped = writer.get('workerStopped')
        stopped = stopped if type(stopped) is bool else None
        if stopped is not True or signal['admissionInProgress'] != 0:
            signal['discardedOnClose'] = None
        queue = writer.get('sharedQueue')
        queue = queue if isinstance(queue, dict) else {}
        shared = {key: counter(queue.get(key)) for key in HOST_SIGNAL_SHARED_COUNTERS}
        observation = {'line': line, 'kind': kind, 'signalWriter': signal, 'workerStopped': stopped,
                       'sharedQueue': shared}
        slots = group['witnesses']
        slots.setdefault('first-retained', observation)
        slots['last-retained'] = observation
        if any(signal[key] for key in ('rejected', 'invalid', 'failed', 'flagDisabled', 'discardedOnClose')):
            slots['latest-signal-loss-witness'] = observation
        if any(shared[key] for key in ('Dropped', 'Failed', 'Discarded')):
            slots['latest-shared-queue-loss-witness'] = observation

    instances = []
    for instance, group in groups.items():
        observations = {}
        for role, row in group['witnesses'].items():
            observations.setdefault(row['line'], {**row, 'roles': []})['roles'].append(role)
        instances.append({'reportedHostInstanceId': instance, 'rowsByKind': dict(group['kinds']),
                          'observations': [observations[line] for line in sorted(observations)]})
    return {'schemaVersion': 1, 'rowsByStatus': dict(statuses), 'maxInstances': HOST_SIGNAL_WRITER_MAX_INSTANCES,
            'instances': instances, 'snapshotAtomic': False,
            'scope': 'retained-member-only;reported-Host-instance;physical-file-order;no-cross-member-counter-sums',
            'signalWriterScope': 'admission-and-worker-outcomes;written-is-reported-append-return-not-durable-delivery',
            'sharedQueueScope': 'cost-and-signal-work;not-attributable-to-signal-rows;no-input-delivery-proof'}


MSI_COST_BUDGET_POLICY = 'VC-MSI-COST-LOG-BUDGET-20261024-R1'
MSI_COST_RECORD_MAX = 16 * 1024
MSI_COST_FAILURE_STAGES = ('flag-read', 'runtime-read', 'sdk-read', 'serialize', 'record-budget', 'append')
MSI_COST_DEFERRED_ROOT_IDENTITY = ('hostInstanceId', 'epoch', 'powerGeneration')


def msi_cost_deferred_window_identity(row):
    """Read a version 3 projection without changing its source row."""
    deferred = row.get('msiCostWindowDeferred')
    if not isinstance(deferred, dict) or type(deferred.get('version')) is not int or deferred['version'] != 3:
        return None
    if any(key in deferred for key in MSI_COST_DEFERRED_ROOT_IDENTITY):
        return {'state': 'mixed-inline-identity', 'reported': None}
    if (deferred.get('logBudgetPolicy') != MSI_COST_BUDGET_POLICY or
            type(deferred.get('recordMaxBytes')) is not int or deferred['recordMaxBytes'] != MSI_COST_RECORD_MAX or
            'kind' in row or 'event' in row or not isinstance(row.get('hostInstanceId'), str) or
            any(type(row.get(key)) is not int or not 0 <= row[key] < 1 << 64 for key in MSI_COST_DEFERRED_ROOT_IDENTITY[1:]) or
            'runId' not in deferred or (deferred['runId'] is not None and not isinstance(deferred['runId'], str)) or
            row.get('runId') != (deferred['runId'] if deferred['runId'] is not None else '')):
        return {'state': 'invalid-window-identity', 'reported': None}
    return {'state': 'reported-same-row-identity', 'reported': {key: row[key] for key in MSI_COST_DEFERRED_ROOT_IDENTITY}}


def msi_cost_budget_evidence(filename, rows):
    if not re.fullmatch(r'input-host-cost\.log(?:\.(?:prev|previous|[0-9]+))?', filename):
        return None
    states, stages, summaries = collections.Counter(), collections.Counter(), collections.Counter()
    first, largest = {}, {}
    identities = collections.Counter()

    def uint(value, maximum):
        return type(value) is int and 0 <= value <= maximum

    def known_window(row, identity):
        deferred = row.get('msiCostWindowDeferred')
        return (isinstance(deferred, dict) and type(deferred.get('version')) is int and
                (deferred['version'] in (1, 2) or
                 (identity is not None and identity['state'] == 'reported-same-row-identity'))
                and deferred.get('logBudgetPolicy') == MSI_COST_BUDGET_POLICY
                and type(deferred.get('recordMaxBytes')) is int and deferred['recordMaxBytes'] == MSI_COST_RECORD_MAX
                and uint(row.get('frames'), (1 << 63) - 1))

    for line, row in rows:
        identity = msi_cost_deferred_window_identity(row)
        if identity is not None:
            identities[identity['state']] += 1
        kind = row.get('kind')
        summary = kind in ('cost-window-summary', 'cost-window-failed')
        if 'diagnosticBudget' not in row:
            if summary:
                states['missing-budget-receipt'] += 1
            elif known_window(row, identity):
                states['window-without-budget-receipt'] += 1
            continue
        budget = row['diagnosticBudget']
        if not isinstance(budget, dict):
            states['invalid-budget-receipt'] += 1
            continue
        version = budget.get('schemaVersion')
        if (type(version) is not int or version not in (1, 2) or
                budget.get('policy') != MSI_COST_BUDGET_POLICY):
            states['unsupported-budget-receipt'] += 1
            continue
        state = budget.get('state')
        if state not in ('bulk-evidence-omitted', 'cost-window-summary', 'cost-window-failed'):
            states['unsupported-budget-receipt'] += 1
            continue
        if version == 2 and state != 'bulk-evidence-omitted':
            states['unsupported-budget-receipt'] += 1
            continue
        original = budget.get('originalRecordBytes')
        pid = row.get('writerPid')
        valid = (uint(pid, (1 << 32) - 1) and pid > 0 and budget.get('completePayloadRetained') is False
                 and type(budget.get('recordMaxBytes')) is int and budget['recordMaxBytes'] == MSI_COST_RECORD_MAX
                 and 'originalRecordBytes' in budget)
        if state == 'bulk-evidence-omitted':
            valid = (valid and not summary and known_window(row, identity) and
                     budget.get('omittedFields') == ['steamDeckPadSubmissions', 'usbipTransport'] and
                     all(name in row and row[name] is None for name in budget['omittedFields']))
        else:
            valid = (valid and kind == state and budget.get('identityTextMayBeTruncated') is True and
                     uint(row.get('capturedFrames'), (1 << 63) - 1))
        if state == 'cost-window-failed':
            valid = valid and original is None
        else:
            valid = valid and uint(original, (1 << 31) - 1) and original > MSI_COST_RECORD_MAX
        if state == 'cost-window-summary':
            valid = valid and budget.get('reason') == 'non-bulk-metadata-over-budget'
        if not valid:
            states['invalid-budget-receipt'] += 1
            continue
        states[state] += 1
        first.setdefault(state, line)
        if original is not None and (state not in largest or original > largest[state]['originalRecordBytes']):
            largest[state] = {'line': line, 'originalRecordBytes': original}
        if state == 'cost-window-failed':
            reason = budget.get('reason')
            stages[reason if reason in MSI_COST_FAILURE_STAGES else 'unknown-stage'] += 1
        elif state == 'bulk-evidence-omitted':
            for name in ('usbipTransportSummary', 'steamDeckPadSubmissionSummary'):
                item = row.get(name)
                shape = 'object-retained' if isinstance(item, dict) else 'absent' if name not in row else 'non-object'
                summaries[(name, shape)] += 1
    if not states and not identities:
        return None
    return {'schemaVersion': 1, 'rowsByState': dict(states), 'failureStages': dict(stages),
            **({'deferredWindowIdentityRowsByState': dict(identities)} if identities else {}),
            'firstReceiptLines': first, 'largestReportedOriginalRecords': largest,
            'bulkSummaryShapes': {name: {state: count for (field, state), count in summaries.items() if field == name}
                                  for name in ('usbipTransportSummary', 'steamDeckPadSubmissionSummary')},
            'scope': 'retained-member-only; budget receipts are diagnostic, not input-frame loss or delivery proof'}


def carrier_policy(name):
    """Original audit entry roles;filename advice never certifies a writer/rate."""
    if not isinstance(name,str):return dict(disposition='ignored',canonicalName=None,lane=None,format=None)
    base=canonical_log_name(name)
    local_launch=re.fullmatch(r'steam-launch\.log(?:\.(?:prev|previous|[0-9]+))?',name) is not None
    if name.startswith('steam/') or (name.startswith('steam-') and base!='steam-original-recovery.jsonl' and not local_launch):
        return dict(disposition='excluded-external',canonicalName=base,lane=None,format=None)
    if '/' in name or not re.search(r'\.(?:log|jsonl)(?:\.(?:prev|previous|\d+))?$',name):
        return dict(disposition='ignored',canonicalName=base,lane=None,format=None)
    if base=='ai-log-index.jsonl':return dict(disposition='export-index-metadata',canonicalName=base,lane=None,format=None)
    lane='observer-detail' if re.fullmatch(r'msi-controller-visibility(?:-wake-\d+)?\.log',base) else LANES.get(base,'unclassified')
    if local_launch:return dict(disposition='inspect',canonicalName=base,lane='launcher-text',format='plain-text')
    if re.fullmatch(r'yeman-fan-host[0-9]{8}\.log',base):return dict(disposition='inspect',canonicalName=base,lane='hc-text',format='plain-text')
    if base=='gamepad-driver-setup.json.hidhide.log':return dict(disposition='inspect',canonicalName=base,lane='external-installer-text',format='plain-text')
    return dict(disposition='inspect',canonicalName=base,lane=lane,format='plain-text' if base in ('app.log','speedhack.log') else 'json-lines')


def native_process_cost_summary(records, current_pid, filename):
    """Offline retained-window statistics; never attribute whole-process CPU to logging."""
    samples, rejected = collections.defaultdict(list), collections.Counter()
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
        if (any(type(v) is not int or not 0 <= v < (1 << 64) for v in numbers) or creation <= 0 or
                begin < 0 or ms <= 0 or end - begin != ms or user < 0 or kernel < 0 or
                value.get('cpuCounterSource') != 'GetProcessTimes' or value.get('cpuCounterUnit') != '100ns'):
            rejected['invalid-counter-provenance-or-window'] += 1
            continue
        if 'schemaVersion' in value:
            schema = value['schemaVersion']
            if type(schema) is not int or not 0 < schema < (1 << 64):
                rejected['invalid-cost-schema-version'] += 1
                continue
            if schema != 1:
                rejected['unsupported-cost-schema-version'] += 1
                continue
        if 'cpuCounterAvailable' in value:
            available = value['cpuCounterAvailable']
            if type(available) is not bool:
                rejected['invalid-counter-availability'] += 1
                continue
            if not available:
                rejected['counter-rate-availability-disagrees'] += 1
                continue
        samples[creation].append((line, creation, begin, end, ms, user + kernel))
    windows = []
    for creation in sorted(samples):
        items = samples[creation]
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
    """Schema-specific field roles, with bounded positive Windows DWORD IDs."""
    def process_id(value):
        if type(value) is int:
            number = value
        elif (isinstance(value, str) and 0 < len(value) <= 20 and value.isascii()
              and value.isdecimal()):
            number = int(value)
        else:
            return None
        return str(number) if 0 < number < (1 << 32) else None
    # Native lifecycle/detail own `pid`; hostPid in their payload is the
    # observed child, not the writer. Keep observer/device PID rules separate.
    if re.fullmatch(r'(?:fan-api|hardware-coordinator)\.log(?:\.(?:prev|previous|\d+))?',filename):
        writer=fan_native_writer_envelope_record(row,filename)
        if writer['state'] in ('reported-pid-and-birth','reported-pid-birth-unavailable'):
            return writer['writerPid'],'writerPid'
        if writer['state']=='legacy-pid-only' and writer['legacyRootPidUsedAsReportedWriter']:
            return writer['writerPid'],'pid'
        return 'unknown','fan-native-writer-envelope-unavailable;subject-pids-not-writer'
    # appendFanLifecycleLog owns pid; hostPid/writerPid are payload subjects.
    if re.fullmatch(r'fan-lifecycle\.log(?:\.(?:prev|previous|\d+))?', filename):
        value = process_id(row.get('pid'))
        if value is not None:
            return value, 'pid'
        return 'unknown', 'fan-native-pid-missing;payload-identity-is-not-writer'
    frontend = re.fullmatch(r'(frontend-trace(?:-detail)?|frontend-errors|webview-failures)\.log(?:\.(?:prev|previous|\d+))?', filename)
    if frontend:
        if 'writerEnvelopeSchema' in row:
            if type(row['writerEnvelopeSchema']) is int and row['writerEnvelopeSchema'] == 1:
                value = process_id(row.get('writerPid'))
                if value is not None:
                    return value, 'writerPid'
            return 'unknown', 'frontend-writer-envelope-invalid;subject-pids-not-writer'
        if (frontend[1] in ('frontend-trace', 'frontend-trace-detail') and
                row.get('origin') == 'renderer-observation' and row.get('where') in
                ('monitor-notice-lifecycle', 'controller-calibration', 'cpu-ccd-visibility')):
            value = process_id(row.get('pid'))
            if value is not None:
                return value, 'pid'
        return 'unknown', 'frontend-writer-envelope-missing;subject-pids-not-writer'
    native = bool(re.fullmatch(r'native-(?:lifecycle|detail)\.log(?:\.(?:prev|previous|\d+))?', filename))
    # Decky's Native logger overwrites processPid at record emission. Its
    # allowed payload pid is a subject (for example a child), never a writer.
    if re.fullmatch(r'decky-sidebar\.log(?:\.(?:prev|previous|\d+))?', filename):
        value = process_id(row.get('processPid'))
        if value is not None:
            return value, 'processPid'
        return 'unknown', 'decky-processPid-missing;payload-pid-is-not-writer'
    observer = filename.startswith('msi-controller-visibility') or event_name(row) in (
        'directinput-device', 'wgi-device', 'msi-pnp-interface')
    keys = ('pid', 'writerPid') if native else (('writerPid', 'observerPid') if observer else ('writerPid', 'observerPid', 'hostPid'))
    for key in keys:
        value = process_id(row.get(key))
        if value is not None:
            return value, key
    if native:
        return 'unknown', 'native-pid-missing; observed-host-is-not-writer'
    if observer:
        return 'unknown', 'observerPid-missing; device-pid-is-not-process'
    # SDK scopeHostPid is the writer only in this named schema, including rotations.
    # It must never become a fallback for native/observer or arbitrary payload subjects.
    sdk = bool(re.fullmatch(r'input-host-xbox-sdk\.log(?:\.(?:prev|previous|\d+))?', filename))
    if sdk and row.get('kind') == 'xbox-sdk-evidence':
        value = process_id(row.get('scopeHostPid'))
        if value is not None:
            return value, 'scopeHostPid'
    transport = row.get('usbipTransport')
    if isinstance(transport, dict):
        value = process_id(transport.get('hostPid'))
        if value is not None:
            return value, 'usbipTransport.hostPid'
    for key in ('pid', 'processId'):
        value = process_id(row.get(key))
        if value is not None:
            return value, key
    return 'unknown', 'not-recorded'


def pid(row, filename=''):
    return process_identity(row, filename)[0]


def stamp(row, filename=''):
    for key in ('timestampUtc', 'timeWithOffset', 'timestampLocal', 'atUtc', 'time', 'timestamp', 'ts'):
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


class _PhysicalLfLines:
    """Replay LF physical lines without retaining a list of copied byte slices.

    Iteration keeps each LF for the JSON parser;selected byte witnesses exclude it.
    Only the original input and a fixed line count live between passes.
    """
    __slots__ = ('data', 'count')

    def __init__(self, data):
        self.data = data
        self.count = data.count(b'\n') + int(bool(data) and not data.endswith(b'\n'))

    def __len__(self):
        return self.count

    def __iter__(self):
        return iter(io.BytesIO(self.data))


class _UnknownLineRanges:
    """Exact ordered unknown lines; varint ranges avoid per-line Python objects."""
    __slots__=('encoded','count','first','last','_start','_end','_encoded_end','_finished')
    def __init__(self):
        self.encoded=bytearray();self.count=0;self.first=None;self.last=None
        self._start=None;self._end=None;self._encoded_end=0;self._finished=False
    def _write(self,value):
        while value>=128:self.encoded.append((value&127)|128);value>>=7
        self.encoded.append(value)
    def _flush(self):
        if self._start is not None:
            self._write(self._start-self._encoded_end);self._write(self._end-self._start+1);self._encoded_end=self._end
    def append(self,line):
        if self._finished or type(line) is not int or self.last is not None and line<=self.last:raise ValueError('unknown lines must be positive,ordered and unique')
        if line<1:raise ValueError('unknown line must be positive')
        if self._start is None:self._start=self._end=line
        elif line==self._end+1:self._end=line
        else:self._flush();self._start=self._end=line
        if self.first is None:self.first=line
        self.last=line;self.count+=1
    def finish(self):
        if not self._finished:self._flush();self.encoded=bytes(self.encoded);self._finished=True
        return self
    def __len__(self):return self.count
    def __getitem__(self,index):
        if not self.count:raise IndexError(index)
        if index==0:return self.first
        if index==-1:return self.last
        raise IndexError('only first/last range coordinates are indexed')
    def ranges(self):
        if not self._finished:raise ValueError('range index is not finished')
        number=shift=0;start=None;end=0
        for byte in self.encoded:
            number|=(byte&127)<<shift
            if byte&128:shift+=7;continue
            if start is None:start=end+number
            else:end=start+number-1;yield start,end;start=None
            number=shift=0
    def __iter__(self):
        for first,last in self.ranges():yield from range(first,last+1)


class _BoundedInvalidRows:
    __slots__=('head','tail','first_kinds','total','counts','rejections','first_rejections','nesting_boundaries','eof_count','partial_tail','last_line','ends_lf')
    def __init__(self,last_line,ends_lf):
        self.head=[];self.tail=collections.deque(maxlen=8);self.first_kinds={};self.total=0;self.counts=collections.Counter();self.rejections=collections.Counter();self.first_rejections={}
        self.nesting_boundaries=collections.Counter()
        self.eof_count=0;self.partial_tail=False;self.last_line=last_line;self.ends_lf=ends_lf
    def __len__(self):return self.total
    def append(self,item):
        self.total+=1;self.counts[item['kind']]+=1
        if item['kind']=='invalid-json-evidence':
            boundary=item.get('jsonNestingEvidence',{}).get('boundary')
            reason=('duplicate-key' if 'duplicateKeyEvidence' in item else
                'non-finite-number' if item.get('parseError')=='non-finite JSON number' else
                'non-standard-number' if item.get('parseError')=='non-standard JSON number' else
                'nesting-limit' if boundary else 'other-rejected-json')
            self.rejections[reason]+=1;self.first_rejections.setdefault((reason,boundary),item)
            if boundary:self.nesting_boundaries[boundary]+=1
        if len(self.head)<8:self.head.append(item)
        self.tail.append(item);eof=item['line']==self.last_line and not self.ends_lf
        self.first_kinds.setdefault((item['kind'],eof),item)
        self.eof_count+=eof;self.partial_tail|=item['kind']=='partial-or-invalid-tail'
    def selected(self,lines):
        candidates={item['line']:item for item in self.head+list(self.tail)+list(self.first_kinds.values())}
        for item in self.first_rejections.values():candidates[item['line']]=item
        selected=set(lines)|{item['line'] for item in self.first_rejections.values()}
        return [candidates[line] for line in sorted(selected)]


class _BoundedNonObjectLines:
    __slots__=('head','tail','total')
    def __init__(self):self.head=[];self.tail=collections.deque(maxlen=8);self.total=0
    def __len__(self):return self.total
    def append(self,line):
        self.total+=1
        if len(self.head)<8:self.head.append(line)
        self.tail.append(line)
    def selected(self):return sorted(set(self.head+list(self.tail)))


class _CompactQuality(dict):
    __slots__=('unknown_lines',)
    def __init__(self,value,unknown_lines):super().__init__(value);self.unknown_lines=unknown_lines


def _quality_error_counts(quality):
    value=quality.get('errorDetailRetention')
    if value is None:return len(quality.get('invalidRows',())),len(quality.get('nonObjectLines',()))
    if not isinstance(value,dict) or type(value.get('schemaVersion')) is not int or value['schemaVersion']!=1:return None,None
    counts=(value.get('invalidRowsObserved'),value.get('nonObjectLinesObserved'))
    if any(type(part) is not int or part<0 for part in counts):return None,None
    if counts[0]<len(quality.get('invalidRows',())) or counts[1]<len(quality.get('nonObjectLines',())):return None,None
    return counts


def _quality_unknown_lines(quality):
    if isinstance(quality,_CompactQuality):return quality.unknown_lines
    value=quality.get('errorDetailRetention')
    if value is not None:
        if not isinstance(value,dict) or type(value.get('schemaVersion')) is not int or value['schemaVersion']!=1:return None
        if value.get('invalidRowsComplete') is not True or value.get('nonObjectLinesComplete') is not True:return None
    return tuple(sorted({item['line'] for item in quality.get('invalidRows',())}|set(quality.get('nonObjectLines',()))))


def invalid_line_byte_evidence(lines, invalid, ends_with_lf):
    """Bounded references to malformed physical LF lines; no repair/inference."""
    bounded=isinstance(invalid,_BoundedInvalidRows)
    head,tail=(invalid.head,list(invalid.tail)) if bounded else (invalid[:8],invalid[-8:])
    kinds = ('invalid-utf8-row', 'invalid-json-row', 'partial-or-invalid-tail', 'invalid-json-evidence')
    witnesses = {}
    for item in (invalid.first_kinds.values() if bounded else invalid):
        key = (item['kind'], item['line'] == len(lines) and not ends_with_lf)
        if key[0] in kinds:
            witnesses.setdefault(key, item)
    chosen = {item['line']: item for item in witnesses.values()}
    for first, last in zip(head, reversed(tail)):
        for item in (first, last):
            if len(chosen) < 16:
                chosen.setdefault(item['line'], item)
    observations, offset = [], 0
    includes_lf = isinstance(lines, _PhysicalLfLines)
    for number, raw in enumerate(lines, 1):
        length = len(raw) - int(includes_lf and raw.endswith(b'\n'))
        if number in chosen:
            raw = memoryview(raw)[:length]
            prefix = raw[:128]
            suffix_at = max(len(prefix), len(raw) - 128)
            suffix = raw[suffix_at:]
            observations.append({'line': number, 'kind': chosen[number]['kind'],
                'byteOffset': offset, 'byteLength': len(raw),
                'terminatedByLf': number < len(lines) or ends_with_lf,
                'unterminatedEndOfFile': number == len(lines) and not ends_with_lf,
                'rawLineSha256': hashlib.sha256(raw).hexdigest(),
                'prefix': {'relativeByteOffset': 0, 'bytes': len(prefix), 'hex': prefix.hex()},
                'suffix': {'relativeByteOffset': suffix_at, 'bytes': len(suffix), 'hex': suffix.hex()},
                'retainedExcerptBytes': len(prefix) + len(suffix), 'excerptComplete': len(raw) <= 256})
        offset += length + 1
    return {'schemaVersion': 1, 'invalidRowsObserved': len(invalid), 'observations': observations,
        'maxListedRows': 16, 'rowsNotListed': len(invalid) - len(observations),
        'maxBytesPerExcerptPart': 128, 'maxExcerptBytesPerLine': 256,
        'maxRetainedSelectionReferences': 24, 'encoding': 'hex-lowercase;no-text-encoding-guess',
        'selection': 'first-known-kind-and-EOF-shape-witnesses-then-balanced-head-tail;physical-file-order',
        'byteCoordinateScope': 'zero-based original member bytes;CR/BOM retained,LF delimiter excluded from each line hash/excerpt',
        'rawFileBytesChanged': False, 'malformedLineWriterInferred': False,
        'copyCutOrSourceDamageCauseProven': False, 'summaryIsCompleteInvalidRowHistory': False}


def load_rows(data,*,compact_quality=False):
    replacement_characters=0;lines=_PhysicalLfLines(data);ends_lf=data.endswith(b'\n')
    rows=[];invalid=_BoundedInvalidRows(len(lines),ends_lf) if compact_quality else []
    non_objects=_BoundedNonObjectLines() if compact_quality else [];unknown=_UnknownLineRanges() if compact_quality else None
    for number,raw in enumerate(lines,1):
        try:line=raw.decode('utf-8-sig' if number==1 else 'utf-8')
        except UnicodeError:
            replacement_characters+=raw.decode('utf-8',errors='replace').count('\ufffd')
            invalid.append({'line':number,'kind':'invalid-utf8-row','parseError':'invalid UTF-8;original bytes retained in archive'})
            if compact_quality:unknown.append(number)
            continue
        replacement_characters+=line.count('\ufffd')
        if not line.strip():continue
        try:row=evidence_json_loads(line)
        except json.JSONDecodeError:
            invalid.append({'line':number,'kind':'partial-or-invalid-tail' if number==len(lines) and not ends_lf else 'invalid-json-row'})
            if compact_quality:unknown.append(number)
            continue
        except ValueError as exc:
            invalid.append({'line':number,'kind':'invalid-json-evidence','parseError':str(exc)[:512],**json_rejection_evidence(exc)})
            if compact_quality:unknown.append(number)
            continue
        if isinstance(row,dict):rows.append((number,row))
        else:
            non_objects.append(number)
            if compact_quality:unknown.append(number)
    invalid_tail=invalid.eof_count if compact_quality else sum(item['line']==len(lines) and not ends_lf for item in invalid)
    partial_tail=invalid.partial_tail if compact_quality else any(item['kind']=='partial-or-invalid-tail' for item in invalid)
    tail_state='empty' if not data else 'lf-delimited' if ends_lf else 'unterminated'
    interpretation=('invalid unterminated EOF row;origin unknown;copy cut possible but not proven' if partial_tail else
        'unterminated EOF evidence rejected for ambiguous JSON/nonfinite number or invalid UTF-8;export cut not inferred' if invalid_tail else
        'unterminated final byte line;no invalid JSON EOF row;missing LF alone does not prove truncation' if tail_state=='unterminated' else
        'no invalid unterminated EOF row;invalid non-tail rows have independent unknown origin')
    quality={'lineCount':len(lines),'validObjectRows':len(rows),'invalidRows':[] if compact_quality else invalid,'nonObjectLines':[] if compact_quality else non_objects,
        'endsWithNewline':ends_lf,'utf8ReplacementCharacters':replacement_characters,'jsonlDelimiter':'LF-byte;Unicode-separators-are-payload',
        'tailBoundary':{'state':tail_state,'invalidUnterminatedEofRows':invalid_tail,'invalidNonTailRows':len(invalid)-invalid_tail,'copyCutProven':False,'sourceDamageProven':False},
        'tailInterpretation':interpretation}
    if invalid:
        byte_evidence=invalid_line_byte_evidence(lines,invalid,ends_lf);quality['invalidLineByteEvidence']=byte_evidence
        if compact_quality:quality['invalidRows']=invalid.selected([row['line'] for row in byte_evidence['observations']])
    if compact_quality:
        unknown.finish()
        if len(unknown)!=len(invalid)+len(non_objects):raise AssertionError('unknown line index count mismatch')
        if invalid or non_objects:
            quality['nonObjectLines']=non_objects.selected()
            quality['errorDetailRetention']={'schemaVersion':1,'scope':'bounded-report-details;original-member-byte-lines-retained',
                'invalidRowsObserved':len(invalid),'invalidRowsListed':len(quality['invalidRows']),'invalidRowsNotListed':len(invalid)-len(quality['invalidRows']),
                'invalidRowsComplete':len(invalid)==len(quality['invalidRows']),'maxListedInvalidRows':24,'invalidRowsByKind':dict(invalid.counts),'invalidEvidenceByReason':dict(invalid.rejections),'maxRetainedDetailedErrorCandidates':32,
                **({'nestingLimitByBoundary':dict(invalid.nesting_boundaries)} if invalid.nesting_boundaries else {}),
                'nonObjectLinesObserved':len(non_objects),'nonObjectLinesListed':len(quality['nonObjectLines']),'nonObjectLinesNotListed':len(non_objects)-len(quality['nonObjectLines']),
                'nonObjectLinesComplete':len(non_objects)==len(quality['nonObjectLines']),'maxListedNonObjectLines':16,
                'uninterpretedPhysicalLines':len(unknown),'firstUninterpretedLine':unknown.first,'lastUninterpretedLine':unknown.last,
                'uninterpretedRangeBytes':len(unknown.encoded),'inMemoryBoundaryIndexComplete':True,'serializedReportContainsEncodedBoundaryIndex':False,
                'originalInputBytesChanged':False,'originalMemberBytesCopiedIntoReport':False,'completeSourceCollectionProven':False}
            quality=_CompactQuality(quality,unknown)
    return rows,quality

def continuity_rows(records, unknown_lines=()):
    """Signal only declared uninterpreted byte-lines, never ordinary gaps.

    Invalid JSON/encoding and nonobject records can hide any writer boundary.
    A known next record starts a fresh comparison window. Blank lines and
    unrelated but readable records are not synthesized into unknown events.
    """
    if unknown_lines is None:
        for line,row in records:yield line,row,True
        return
    if isinstance(unknown_lines,_UnknownLineRanges):
        ranges=iter(unknown_lines.ranges());boundary=next(ranges,None)
        for line,row in records:
            reset=False
            while boundary is not None and boundary[0]<line:
                reset=True
                if boundary[1]>=line:boundary=(line,boundary[1]);break
                boundary=next(ranges,None)
            yield line,row,reset
        return
    unavailable = iter(sorted(set(unknown_lines)))
    boundary = next(unavailable, None)
    for line, row in records:
        reset = False
        while boundary is not None and boundary < line:
            reset = True
            boundary = next(unavailable, None)
        yield line, row, reset


def repeated_recovery_fallback_observations(records, filename='native-lifecycle.log', *, unknown_lines=()):
    """Same received queue refusal; no retry/action or whole-lifetime inference."""
    if not re.fullmatch(r'native-lifecycle\.log(?:\.(?:prev|previous|\d+))?', filename):
        return []
    states, repeated = {}, []
    envelope = {'time', 'event', 'pid', 'hwnd', 'powerLifecycle', 'powerGeneration',
                'observedPowerGeneration', 'writerProcessCreation100ns'}
    for line, row, reset in continuity_rows(records, unknown_lines):
        if reset:
            states.clear()
        event, writer = event_name(row), pid(row, filename)
        if event == 'boot-single-instance-acquired':
            states = {} if writer == 'unknown' else {key:value for key,value in states.items() if key[0] != writer}
            continue
        if event != 'input-host-recovery-fallback':
            continue
        if writer == 'unknown':
            states.clear()
            continue
        generation, birth = row.get('powerGeneration'), row.get('writerProcessCreation100ns')
        valid_birth = birth is None or (isinstance(birth, str) and 0 < len(birth) <= 20 and birth.isascii()
            and birth.isdecimal() and 0 < int(birth) < (1 << 64))
        if (row.get('state') != 'queue-refused' or type(generation) is not int or not 0 <= generation < (1 << 64)
                or not valid_birth or any(type(row.get(key)) is not int or not 0 < row[key] < (1 << 64)
                                         for key in ('serial', 'requestId'))):
            states = {key:value for key,value in states.items() if key[0] != writer}
            continue
        key = writer, birth
        try:
            signature = json.dumps({name:value for name,value in row.items() if name not in envelope},
                                   sort_keys=True, ensure_ascii=False, separators=(',', ':'), allow_nan=False)
        except (ValueError, TypeError, RecursionError):
            states.pop(key, None)
            continue
        current = generation, signature
        if states.get(key) == current:
            repeated.append((line, row))
        states[key] = current
    return repeated


def repeated_quarantine_observations(records, filename='native-lifecycle.log', *, unknown_lines=()):
    """Same received wire state; never infer duplicate actions or missing probes."""
    if not re.fullmatch(r'native-lifecycle\.log(?:\.(?:prev|previous|\d+))?', filename):
        return {}
    states, repeated = {}, collections.defaultdict(list)
    envelope = {'time', 'event', 'pid', 'hwnd', 'powerLifecycle', 'powerGeneration', 'writerProcessCreation100ns'}
    probe, held = 'input-host-quarantine-endpoint-probe', 'input-host-quarantine-restore-held'
    for line, row, reset in continuity_rows(records, unknown_lines):
        if reset:
            states.clear()
        event = row.get('event')
        writer = pid(row, filename)
        if event in ('input-host-quarantine-cleared', 'boot-single-instance-acquired'):
            if writer == 'unknown':
                states.clear()  # Do not prove repeats across an unattributed boundary.
            else:
                states = {key: value for key, value in states.items() if key[0] != writer}
            continue
        if event not in (probe, held):
            continue
        if writer == 'unknown':
            states.clear()  # An unattributed target row may cross any writer boundary.
            continue
        generation = row.get('powerGeneration')
        if type(generation) is not int or not 0 <= generation < (1 << 64):
            states = {key: value for key, value in states.items() if key[0] != writer}
            continue
        birth = row.get('writerProcessCreation100ns')
        if birth is not None and not (isinstance(birth, str) and 0 < len(birth) <= 20 and birth.isascii()
                and birth.isdecimal() and 0 < int(birth) < (1 << 64)):
            states = {key: value for key, value in states.items() if key[0] != writer}
            continue
        if event == held:
            producer = 'held'
        elif 'owner' not in row and 'ownedSdReturnCandidate' not in row:
            producer = 'bus'
        elif row.get('owner') == 'capture-lifecycle-thread':
            producer = 'lifecycle'
        else:
            states = {key: value for key, value in states.items() if key[0] != writer}
            continue
        key = writer, birth, producer
        payload = {name: value for name, value in row.items() if name not in envelope}
        try:
            signature = json.dumps(payload, sort_keys=True, ensure_ascii=False, separators=(',', ':'), allow_nan=False)
        except (ValueError, TypeError, RecursionError):
            states.pop(key, None)
            continue
        current = generation, signature
        if states.get(key) == current:
            repeated[event].append((line, row))
        states[key] = current
    return dict(repeated)


def _snapshot_comparison_birth(row):
    """Reported wire token only; neither a PID nor this token verifies OS lifetime."""
    if 'writerProcessCreation100ns' not in row:
        return ('missing',)
    value = row['writerProcessCreation100ns']
    if value is None:
        return ('null',)
    if (isinstance(value, str) and 0 < len(value) <= 20 and value.isascii()
            and value.isdecimal() and 0 < int(value) < (1 << 64)):
        return ('reported', value)
    return None


def _snapshot_comparison_scope(previous, row, filename, target):
    process, event = pid(row, filename), event_name(row)
    if event == 'boot-single-instance-acquired':
        if process == 'unknown':
            previous.clear()
        else:
            previous.pop(process, None)
        return None
    prior = previous.get(process)
    # Readable non-target rows only split comparisons when they explicitly
    # report a different/invalid writer birth or power generation.
    if prior is not None and (
            ('writerProcessCreation100ns' in row and _snapshot_comparison_birth(row) != prior[0]) or
            ('powerGeneration' in row and (type(row['powerGeneration']) is not int or
             not 0 <= row['powerGeneration'] < (1 << 64) or row['powerGeneration'] != prior[1]))):
        previous.pop(process, None)
    if event != target:
        return None
    if process == 'unknown':
        previous.clear()
        return None
    birth, generation = _snapshot_comparison_birth(row), row.get('powerGeneration')
    if birth is None or type(generation) is not int or not 0 <= generation < (1 << 64):
        previous.pop(process, None)
        return None
    return process, birth, generation


def duplicate_snapshots(records, filename="native-lifecycle.log", *, unknown_lines=()):
    # One most-recent received snapshot per writer; observed scope changes
    # rearm it. Missing/null birth stays unknown, never an OS-lifetime proof.
    previous, duplicates = {}, []
    for line, row, reset in continuity_rows(records, unknown_lines):
        if reset:
            previous.clear()
        scope = _snapshot_comparison_scope(previous, row, filename, 'rog.hid-discovery-summary')
        if scope is None:
            continue
        process, birth, generation = scope
        context = {key: row[key] for key in ('runId', 'hostInstanceId') if key in row}
        semantic = {key: value for key, value in row.items() if key not in TRANSPORT_FIELDS}
        try:
            signature = json.dumps([context, semantic], sort_keys=True, allow_nan=False)
        except (ValueError, TypeError, OverflowError, RecursionError):
            previous.pop(process, None)
            continue
        current = birth, generation, signature
        if previous.get(process) == current:
            duplicates.append((line, row))
        previous[process] = current
    return duplicates


def repeated_guard_successes(records, filename="native-lifecycle.log", *, unknown_lines=()):
    previous, repeated = {}, []
    for line, row, reset in continuity_rows(records, unknown_lines):
        if reset:
            previous.clear()
        scope = _snapshot_comparison_scope(previous, row, filename, 'oem-vendor-stack')
        if scope is None:
            continue
        process, birth, generation = scope
        state = row.get('state')
        if state == 'wake-guard-started':
            previous.pop(process, None)
        if state != 'wake-guard-pass':
            continue
        counters = ('requestedEpoch', 'durationMs')
        semantic = {k: v for k, v in row.items() if k not in TRANSPORT_FIELDS and k not in counters}
        # Counter presence and JSON wire type remain evidence. Only values of
        # reported uint64-compatible counters are excluded from equality.
        shapes = {key: ['missing'] if key not in row else ['uint64-counter']
                  if type(row[key]) is int and 0 <= row[key] < (1 << 64)
                  else ['semantic-value', row[key]] for key in counters}
        context = {key: row[key] for key in ('runId', 'hostInstanceId') if key in row}
        try:
            signature = json.dumps([context, semantic, shapes], sort_keys=True, allow_nan=False)
        except (ValueError, TypeError, OverflowError, RecursionError):
            previous.pop(process, None)
            continue  # Uninterpretable values cannot bridge a repeat claim.
        current = birth, generation, signature
        if row.get('ok') is True and previous.get(process) == current:
            repeated.append((line, row))
        previous[process] = current  # Failures/unknown make next success a recovery.
    return repeated


def frame_comparison_context(row):
    """Typed v1/v2 comparison only; unsupported evidence stays uninterpreted."""
    uint = lambda value: type(value) is int and 0 <= value < (1 << 64)
    version = row.get('formatVersion')
    if type(version) is not int or version not in (1, 2):
        return None, 'unsupported-format-version'
    generation = row.get('powerGeneration')
    if not uint(generation):
        return None, 'invalid-native-generation'
    birth = row.get('writerProcessCreation100ns')
    if 'writerProcessCreation100ns' in row and not (
            isinstance(birth, str) and 0 < len(birth) <= 20 and birth.isascii()
            and birth.isdecimal() and 0 < int(birth) < (1 << 64)):
        return None, 'invalid-writer-birth'
    reason = row.get('snapshotReason')
    if ('diagnosticRecoveryReoffer' in row
            and type(row['diagnosticRecoveryReoffer']) is not bool):
        return None, 'invalid-diagnostic-recovery-marker'
    if row.get('diagnosticRecoveryReoffer') is True and (version != 2 or reason != 'state-change'):
        return None, 'unsupported-diagnostic-recovery-context'
    source = row.get('source')
    inactive = None
    if isinstance(source, dict) and 'inactiveWithoutSource' in source:
        inactive = source['inactiveWithoutSource']
        if version != 2 or type(inactive) is not bool:
            return None, 'invalid-inactive-source-context'
    if isinstance(source, dict) and 'inactiveTopologySample' in source:
        if reason != 'periodic' or source['inactiveTopologySample'] is not True or inactive is not True:
            return None, 'invalid-inactive-topology-sample'
    if reason == 'periodic':
        return (version, generation, birth, None), None
    if version == 2 and reason != 'state-change':
        return None, 'unsupported-snapshot-reason'
    if not isinstance(source, dict) or source.get('snapshotKnown') is not True:
        return None, 'source-context-unknown'
    if (not isinstance(source.get('inputSource'), str) or not source['inputSource']
            or any(type(source.get(k)) is not bool for k in ('live', 'readContextStable'))
            or any(not uint(source.get(k)) for k in (
                'powerGeneration', 'topologyGeneration', 'primarySlot', 'admittedMask'))):
        return None, 'invalid-source-context-fields'
    if (not isinstance(row.get('hostSubmission'), str) or not row['hostSubmission']
            or type(row.get('semanticNeutral')) is not bool):
        return None, 'invalid-frame-context-fields'
    publication_reason, host_active = row.get('publicationReason'), row.get('hostActive')
    if (publication_reason is not None and (not isinstance(publication_reason, str) or
            not 0 < len(publication_reason) <= 96 or not publication_reason.isascii()) or
            host_active is not None and type(host_active) is not bool):
        return None, 'invalid-publication-context'
    proof = row.get('frameProof')
    if not isinstance(proof, dict):
        return None, 'frame-proof-unavailable'
    receipt = proof.get('hostReceipt')
    if receipt is None:
        host = ('', '', 0)  # Matches the producer's absent/null receipt defaults.
    elif (isinstance(receipt, dict) and uint(receipt.get('epoch'))
            and all(isinstance(receipt.get(k), str) for k in ('hostInstanceId', 'runId'))):
        host = receipt['hostInstanceId'], receipt['runId'], receipt['epoch']
    else:
        return None, 'invalid-host-receipt-context'
    signature = (row['hostSubmission'], *host, inactive, publication_reason, host_active,
        *(source[k] for k in ('inputSource', 'powerGeneration', 'topologyGeneration',
                            'primarySlot', 'admittedMask', 'live', 'readContextStable')))
    return (version, generation, birth, signature), None


def repeated_frame_observations(records, filename='native-lifecycle.log', *, unknown_lines=(), evidence=None):
    """Only comparable contexts can prove a repeat; unknowns clear the writer.

    Ordinary neutrality/timing remain sampled. A known-version explicit periodic
    row proves its own lane classification even without a comparable context.
    No invalid frame becomes history for the next valid observation.
    """
    previous, repeats = {}, []

    def reject(line, process, reason):
        if process == 'unknown':
            previous.clear()
        else:
            previous.pop(process, None)
        if evidence is None:
            return
        if not evidence:
            evidence.update(schemaVersion=1, rejectedRows=0, reasonCounts={},
                firstLine=line, lastLine=line, references=[], referencesOmitted=0,
                maxReferences=8, comparisonOnly=True, businessFailureInferred=False,
                scope='known-v1-v2-context-comparison;not-input-delivery-or-runtime-authority')
        evidence['rejectedRows'] += 1
        evidence['lastLine'] = line
        counts = evidence['reasonCounts']
        counts[reason] = counts.get(reason, 0) + 1
        refs = evidence['references']
        item = {'line': line, 'reason': reason}
        if len(refs) < 8:
            refs.append(item)
        else:
            refs[4:] = refs[5:] + [item]
        evidence['referencesOmitted'] = evidence['rejectedRows'] - len(refs)

    for line, row, reset in continuity_rows(records, unknown_lines):
        if reset:
            previous.clear()
        process = pid(row, filename)
        if event_name(row) == 'boot-single-instance-acquired':
            if process == 'unknown':
                previous.clear()
            else:
                previous.pop(process, None)
        if event_name(row) != 'input-host-frame-evidence':
            continue
        if process == 'unknown':
            reject(line, process, 'writer-unattributed')
            continue
        context, reason = frame_comparison_context(row)
        if reason:
            reject(line, process, reason)
            continue
        if row.get('snapshotReason') == 'periodic':
            repeats.append((line, row))
            previous.pop(process, None)
            continue
        if row.get('diagnosticRecoveryReoffer') is True:
            # Producer-reported log recovery breaks comparison continuity only.
            # It proves neither source recovery nor delivery, and seeds no history.
            previous.pop(process, None)
            continue
        if previous.get(process) == context:
            repeats.append((line, row))
        previous[process] = context
    return repeats


_LATE_HIDE_STAGES = ('late-hide-enqueue', 'late-hide-dequeue', 'late-hide-exec-decision', 'late-hide-complete')


def _late_hide_receipt(row, key):
    value = row.get(key)
    return value if isinstance(value, str) and value else 'unknown'


def _late_hide_stage_witness(line, row, stage):
    reason = row.get('reason')
    witness = {'line': line, 'reason': reason if isinstance(reason, str) else None}
    if stage in ('late-hide-enqueue', 'late-hide-complete'):
        witness['outcome'] = _late_hide_receipt(row, 'outcome')
    elif stage == 'late-hide-exec-decision':
        witness['decision'] = _late_hide_receipt(row, 'decision')
    return witness


def _late_hide_token_evidence(token, stages):
    enqueue = stages[_LATE_HIDE_STAGES[0]]
    accepted = []
    coalesced = []
    for line, row in enqueue:
        outcome = row.get('outcome')
        if (outcome in ('accepted', 'coalesced') and
                row.get('coalesced', outcome == 'coalesced') is (outcome == 'coalesced') and
                row.get('alreadyCompleted', False) is False):
            (accepted if outcome == 'accepted' else coalesced).append(line)
    worker = [stages[name] for name in _LATE_HIDE_STAGES[1:]]
    # Ordered producers may reuse a token; legacy coalescing may publish a
    # new one. A coalesced receipt alone never proves a new queue admission.
    state = 'partial'
    if len(accepted) + len(coalesced) != len(enqueue):
        state = 'unsupported-enqueue'
    elif len(accepted) > 1 or (not accepted and len(coalesced) > 1) or any(len(stage) > 1 for stage in worker):
        state = 'ambiguous-stage'
    elif (accepted or coalesced) and all(worker):
        lines = [(accepted or coalesced)[0], *(stage[0][0] for stage in worker)]
        if all(a < b for a, b in zip(lines, lines[1:])):
            state = 'ordered-accepted' if accepted else 'ordered-coalesced-only'
        else:
            state = 'out-of-order'
    return {'queueToken': token, 'state': state,
            'receipts': {name: [_late_hide_stage_witness(line, row, name) for line, row in stages[name]]
                         for name in _LATE_HIDE_STAGES}}


def late_hide_episodes(records, current_pid, filename):
    """Correlate retained receipts from one archive member occurrence."""
    fields = ('powerGeneration', 'hostGen', 'recordGen', 'sourceGen')
    identity_fields = ('nativeWriterPid', 'writerProcessCreation100ns', 'runId', *fields)
    groups = collections.defaultdict(list)
    missing = []
    rejections = collections.Counter()
    volume = collections.Counter()
    enqueue_outcomes = collections.Counter()
    decisions = collections.Counter()
    completion_outcomes = collections.Counter()
    for line, row in records:
        stage = event_name(row)
        if stage not in _LATE_HIDE_STAGES:
            continue
        volume[stage] += 1
        if stage == _LATE_HIDE_STAGES[0]:
            enqueue_outcomes[_late_hide_receipt(row, 'outcome')] += 1
        elif stage == _LATE_HIDE_STAGES[2]:
            decisions[_late_hide_receipt(row, 'decision')] += 1
        elif stage == _LATE_HIDE_STAGES[3]:
            completion_outcomes[_late_hide_receipt(row, 'outcome')] += 1
        writer = row.get('pid')  # Native envelope; do not fall back to a subject PID.
        birth = _snapshot_comparison_birth(row)
        run = row.get('runId')
        generations = tuple(row.get(field) for field in fields)
        if not _control_uint(writer, 32) or writer == 0:
            rejection = 'invalid-native-writer-pid'
        elif birth is None:
            rejection = 'invalid-writer-birth'
        elif birth[0] != 'reported':
            rejection = 'writer-birth-unavailable'
        elif not isinstance(run, str) or not run.strip():
            rejection = 'invalid-run-id'
        elif not all(_control_uint(value) for value in generations):
            rejection = 'invalid-generation'
        else:
            rejection = None
        if rejection is not None:
            missing.append(line)
            rejections[rejection] += 1
            continue
        groups[(writer, birth[1], run, *generations)].append((line, row, stage))
    summaries = []
    for key, items in groups.items():
        tokens = collections.defaultdict(lambda: collections.defaultdict(list))
        no_token = 0
        by_stage = collections.defaultdict(list)
        for line, row, stage in items:
            by_stage[stage].append((line, row))
            token = row.get('queueToken')
            if _control_uint(token) and token > 0:
                tokens[token][stage].append((line, row))
            else:
                no_token += 1
        complete = by_stage[_LATE_HIDE_STAGES[3]]
        evidence = [_late_hide_token_evidence(token, stages) for token, stages in tokens.items()]
        states = collections.Counter(item['state'] for item in evidence)
        duplicate_stages = [token for token, stages in tokens.items()
                            if any(len(stages[name]) > 1 for name in _LATE_HIDE_STAGES[1:])]
        span_ms = None
        stamps = [stamp(row, filename) for _, row in complete]
        try:
            if len(stamps) >= 2:
                span_ms = round((datetime.datetime.fromisoformat(stamps[-1]) - datetime.datetime.fromisoformat(stamps[0])).total_seconds() * 1000)
        except (ValueError, TypeError):
            pass
        summaries.append({'identity': dict(zip(identity_fields, key)),
                          **row_stats([(line, row) for line, row, _ in items], current_pid, filename),
                          'eventCounts': dict(collections.Counter(stage for _, _, stage in items)),
                          'enqueueOutcomes': dict(collections.Counter(_late_hide_receipt(row, 'outcome') for _, row in by_stage[_LATE_HIDE_STAGES[0]])),
                          'decisions': dict(collections.Counter(_late_hide_receipt(row, 'decision') for _, row in by_stage[_LATE_HIDE_STAGES[2]])),
                          'completionOutcomes': dict(collections.Counter(_late_hide_receipt(row, 'outcome') for _, row in complete)),
                          'uniquePositiveQueueTokens': len(tokens), 'tokensWithExactlyOneEachStage': states['ordered-accepted'],
                          'tokensWithOrderedCoalescedObservation': states['ordered-coalesced-only'],
                          'tokensWithRepeatedDequeueDecisionOrComplete': duplicate_stages,
                          'rowsWithoutPositiveQueueToken': no_token,
                          'queueTokenEvidence': evidence,
                          'completionSpanMs': span_ms,
                          'completionExampleLines': [line for line, _ in complete[:2]] + ([complete[-1][0]] if len(complete) > 2 else []),
                          'classification': 'transaction-volume; not periodic-sample violation',
                          'evidenceScope': 'logged receipts within this member; token correlation is not hardware/Steam acceptance proof'})
    return {'episodes': sorted(summaries, key=lambda item: -item['counts']['all']),
            'rowsWithIncompleteIdentity': missing,
            'identityRejections': dict(rejections),
            'receiptVolume': {'eventCounts': dict(volume), 'enqueueOutcomes': dict(enqueue_outcomes),
                              'decisions': dict(decisions), 'completionOutcomes': dict(completion_outcomes)},
            'groupingFields': list(identity_fields),
            'tokenScope': 'positive queueToken; one accepted enqueue and one of each worker stage in member order; coalesced-only chains counted separately',
            'rotationScope': 'one member occurrence only; truncated/overlapping rotations not inferred or merged'}


def _consumer_proof_value_valid(kind, value):
    if kind == 'bool':
        return type(value) is bool
    if kind == 'uint':
        return value is None or (type(value) is int and 0 <= value < (1 << 64))
    if kind == 'filetime':
        return value is None or (isinstance(value, str) and 0 < len(value) <= 20 and
            value.isascii() and value.isdecimal() and int(value) < (1 << 64))
    return False


def _consumer_proof_clock_category(source, flag, now_key, sample_key, units_per_ms):
    # This is an offline comparison of the same received fields, not the
    # producer's short-circuit predicate or a new clock observation.
    if source.get(flag) is not True:
        return 'not-evaluated' if source.get(flag) is False else 'evaluation-unknown'
    now, sample = source.get(now_key), source.get(sample_key)
    if now is None or sample is None:
        return 'clock-values-not-recorded'
    kind = 'filetime' if units_per_ms == 10000 else 'uint'
    if not _consumer_proof_value_valid(kind, now) or not _consumer_proof_value_valid(kind, sample):
        return 'clock-values-invalid'
    now, sample = int(now), int(sample)
    if units_per_ms == 10000 and (now == 0 or sample == 0):
        return 'zero-filetime-not-a-timestamp'
    if now < sample:
        return 'clock-order-invalid'
    limit = source.get('proofLimitMs')
    if type(limit) is not int or not _consumer_proof_value_valid('uint', limit):
        return 'clock-pair-valid-limit-unavailable'
    delta, bound = now - sample, limit * units_per_ms
    return 'less-than-recorded-limit' if delta < bound else 'greater-than-recorded-limit' if delta > bound else 'equal-to-recorded-limit'


def consumer_proof_selection_fields(readiness):
    """Fixed selection categories; original clocks/flags stay in the row view."""
    def state(obj, key):
        if key not in obj:
            return 'missing'
        value = obj[key]
        if value is None:
            return 'null'
        if type(value) is bool:
            return 'bool-true' if value else 'bool-false'
        return 'non-boolean'
    source = readiness.get('consumerObservation')
    fields = {'observationState': 'missing' if 'consumerObservation' not in readiness else
        'null' if source is None else 'object' if isinstance(source, dict) else 'invalid-container'}
    source = source if isinstance(source, dict) else {}
    for name in ('diagnosticOnly', 'authority', 'sharedPhaseEvaluated', 'interactiveProofEvaluated', 'kernelProofEvaluated'):
        fields[name + 'State'] = state(source, name)
    for name, flag, now, sample, units in (
        ('interactiveClock', 'interactiveProofEvaluated', 'proofNowFileTime', 'interactiveFileTime', 10000),
        ('completedClock', 'interactiveProofEvaluated', 'proofNowFileTime', 'exitFileTime', 10000),
        ('kernelClock', 'kernelProofEvaluated', 'kernelProofNowFileTime', 'kernelEventFileTime', 10000),
        ('armedClock', 'kernelProofEvaluated', 'nowTick', 'armedTick', 1)):
        fields[name] = _consumer_proof_clock_category(source, flag, now, sample, units)
    return fields


def consumer_proof_evidence(readiness):
    """Retain producer observations; arithmetic uses only the same recorded clocks.

    An age/limit comparison is not the original short-circuit guard result.
    In particular an unvisited proof branch has no inferred age or permission.
    """
    readiness = readiness if isinstance(readiness, dict) else {}
    source = readiness.get('consumerObservation')
    result = {'schemaVersion': 1, 'source': 'readiness.consumerObservation',
        'status': 'not-recorded' if 'consumerObservation' not in readiness else 'unrecognized-observation',
        'observation': {}, 'fieldIssues': [], 'ages': {},
        'guardTagRecorded': any(isinstance(readiness.get(key), str) and 0 < len(readiness[key]) <= 256
            for key in ('rejectedGuard', 'rejectionReason')),
        'guardFieldPresent': any(key in readiness for key in ('rejectedGuard', 'rejectionReason')),
        'scope': 'producer-reported-same-record-clocks;not-action-or-resource-return-authority',
        'ageComparisonIsNotGuardEvaluation': True, 'currentClockRead': False,
        'businessPredicateReexecuted': False, 'permissionInferred': False}
    if not isinstance(source, dict):
        return result
    result['status'] = 'recorded-observation'
    observed = result['observation']
    bool_fields = ('diagnosticOnly', 'authority', 'sharedPhaseEvaluated', 'interactiveProofEvaluated', 'kernelProofEvaluated')
    uint_fields = ('generation', 'configEpoch', 'sharedPhase', 'nowTick', 'armedTick', 'proofLimitMs')
    filetime_fields = ('proofNowFileTime', 'kernelProofNowFileTime', 'interactiveFileTime', 'exitFileTime', 'kernelEventFileTime')
    for key in bool_fields + uint_fields + filetime_fields:
        if key not in source:
            continue
        value = source[key]
        kind = 'bool' if key in bool_fields else 'uint' if key in uint_fields else 'filetime'
        valid = _consumer_proof_value_valid(kind, value)
        if valid:
            observed[key] = value
        else:
            result['fieldIssues'].append({'field': key, 'issue': 'unexpected-type-or-out-of-range'})

    for name, flag, now_key, sample_key, units_per_ms in (
        ('interactive', 'interactiveProofEvaluated', 'proofNowFileTime', 'interactiveFileTime', 10000),
        ('completed', 'interactiveProofEvaluated', 'proofNowFileTime', 'exitFileTime', 10000),
        ('kernelEvent', 'kernelProofEvaluated', 'kernelProofNowFileTime', 'kernelEventFileTime', 10000),
        ('operationSinceArmed', 'kernelProofEvaluated', 'nowTick', 'armedTick', 1)):
        evaluated = observed.get(flag)
        age = {'status': 'evaluation-unknown', 'evaluationField': flag,
               'nowField': now_key, 'sampleField': sample_key,
               'clockDomain': 'Windows-FILETIME-100ns' if units_per_ms == 10000 else 'GetTickCount64-ms',
               'derivedOffline': True}
        result['ages'][name] = age
        if evaluated is not True:
            if evaluated is False:
                age['status'] = 'not-evaluated'
            continue
        now, sample = observed.get(now_key), observed.get(sample_key)
        if now is None or sample is None:
            age['status'] = 'clock-values-not-recorded'
            continue
        if units_per_ms == 10000:
            now, sample = int(now), int(sample)
            if now == 0 or sample == 0:
                age['status'] = 'zero-filetime-not-a-timestamp'
                continue
        if now < sample:
            age['status'] = 'clock-order-invalid'
            continue
        delta = now - sample
        whole, fraction = divmod(delta, units_per_ms)
        fractional_text = ('%04d' % fraction).rstrip('0') if fraction else ''
        age.update(status='derived-from-recorded-clock-pair', deltaNativeUnits=str(delta),
                   nativeUnit='100ns' if units_per_ms == 10000 else 'ms',
                   ageMsDecimal=str(whole) + ('.' + fractional_text if fractional_text else ''))
        limit = observed.get('proofLimitMs')
        if type(limit) is int:
            bound = limit * units_per_ms
            age['recordedLimitMs'] = limit
            age['comparisonToRecordedLimit'] = 'less-than' if delta < bound else 'greater-than' if delta > bound else 'equal'
    return result


MAX_MENU_TRACE_SNAPSHOTS = 128
MENU_TRACE_EVENTS = ('screen-standalone-special', 'shortcut-frontend-button')


def native_menu_route_evidence(value):
    """Bounded original alias calls;lookup/state observations never attest input."""
    view = {'state': 'null' if value is None else 'invalid-type', 'reported': {},
        'fieldIssues': [], 'calls': [], 'callsFieldState': 'unavailable', 'callsNotListed': None,
        'maxListedCalls': 16, 'callerIdentityInferred': False, 'nativeInputAcceptanceInferred': False,
        'fieldStateEncoding': 'only-missing-invalid-or-unsupported;reported-values-preserve-normal-and-null',
        'physicalDeliveryInferred': False, 'routeOrWriterIdentityBackfilled': False,
        'readingRules': [
            'lookup/list/state calls are already executed wrapper observations;callerCallback and nativeInputAcceptance remain UNKNOWN',
            'appid,pid,gameid64 are original lookup/state arguments,not Native writer identity;roles stay producer-reported',
            'inputTrace and previousInputTrace keep their own alias identity;never merge histories or relabel previous calls',
            'original and registered-fallback are distinct calls;no request counts,winning wrapper or final native acceptance inferred',
            'returned/held-window/array or state2 does not prove activation callback or physical input delivery',
            'null heldOccurrences with arrayLength records unknown array slots;do not convert to zero matches',
            'observedWindowMs is the producer-capped window observation;not wall-time cleanup or an expiry timer proof']}
    if not isinstance(value, dict):
        return view
    metadata = ('schema', 'diagnosticOnly', 'scope', 'attempted', 'status', 'reason', 'observedWindowMs',
        'eventCount', 'maxEvents', 'windowLimitMs', 'callerCallback', 'finalWrapperSelectionProven',
        'nativeInputAcceptance', 'physicalInputProven')
    booleans = ('diagnosticOnly', 'attempted', 'finalWrapperSelectionProven', 'physicalInputProven')
    def clock(part):
        return type(part) in (int, float) and 0 <= part <= 10000 and math.isfinite(part)
    def uint(part, maximum):
        return type(part) is int and 0 <= part <= maximum
    def gameid(part):
        return (isinstance(part, str) and 0 < len(part) <= 20 and part.isascii() and
            part.isdecimal() and part[0] != '0' and int(part) < (1 << 64))
    issues = view['fieldIssues']
    for key in metadata:
        if key not in value:
            issues.append(key + ':missing');continue
        part = value[key]
        valid = (type(part) is bool if key in booleans else
            part is None or clock(part) if key == 'observedWindowMs' else
            uint(part, 9007199254740991) if key in ('schema', 'eventCount', 'maxEvents', 'windowLimitMs') else
            isinstance(part, str) and len(part) <= 256)
        if valid:
            view['reported'][key] = part
        else:
            issues.append(key + ':invalid-type-or-range')
    known = (type(value.get('schema')) is int and value['schema'] == 1 and
        value.get('diagnosticOnly') is True and value.get('scope') == 'existing-alias-original-calls')
    if known:
        if type(value.get('maxEvents')) is not int or value['maxEvents'] != 16:
            issues.append('maxEvents:unsupported-limit')
        if type(value.get('windowLimitMs')) is not int or value['windowLimitMs'] != 10000:
            issues.append('windowLimitMs:unsupported-limit')
        if value.get('status') not in ('not-started', 'observing', 'stopped'):
            issues.append('status:unsupported-value')
        for key, expected in (('callerCallback', 'UNKNOWN'), ('nativeInputAcceptance', 'UNKNOWN'),
            ('finalWrapperSelectionProven', False), ('physicalInputProven', False)):
            if type(value.get(key)) is not type(expected) or value[key] != expected:
                issues.append(key + ':unsupported-proof-claim')
    fields = ('ordinal', 'elapsedMs', 'kind', 'path', 'receiverMatches', 'appid', 'appidRole',
        'pid', 'pidRole', 'gameid64', 'gameidRole', 'state', 'outcome', 'resultKind', 'arrayLength', 'heldOccurrences')
    enums = {'kind': ('lookup', 'list', 'state'), 'path': ('original', 'registered-fallback', 'alias-submit'),
        'appidRole': ('logical', 'registered', 'other', 'unknown', 'not-applicable'),
        'pidRole': ('held-host', 'game', 'zero', 'other', 'unknown', 'not-applicable'),
        'gameidRole': ('logical', 'registered', 'other', 'unknown', 'not-applicable'),
        'outcome': ('returned', 'threw'), 'resultKind': ('held-window', 'none', 'other', 'throw', 'array')}
    calls = value.get('calls')
    view['callsFieldState'] = ('missing' if 'calls' not in value else 'null' if calls is None else
        'array' if isinstance(calls, list) else 'invalid-type')
    if isinstance(calls, list):
        view['callsNotListed'] = max(0, len(calls) - 16)
        if len(calls) > 16:
            issues.append('calls:over-original-capacity')
        if type(value.get('eventCount')) is int and value['eventCount'] != len(calls):
            issues.append('eventCount:calls-length-disagrees')
        if value.get('attempted') is False and calls:
            issues.append('attempted:false-with-calls')
        for index, call in enumerate(calls[:16]):
            item = {'callIndex': index, 'reported': {}, 'fieldStates': {}, 'fieldIssues': []}
            view['calls'].append(item)
            if not isinstance(call, dict):
                item['fieldIssues'].append('call:invalid-type')
                issues.append('calls[' + str(index) + ']:invalid-observation');continue
            for key in fields:
                if key not in call:
                    item['fieldStates'][key] = 'missing';item['fieldIssues'].append(key + ':missing');continue
                part = call[key]
                valid = (uint(part, 16) and part > 0 if key == 'ordinal' else clock(part) if key == 'elapsedMs' else
                    type(part) is bool if key == 'receiverMatches' else
                    part is None or type(part) is int and -(1 << 31) <= part < (1 << 32) if key == 'appid' else
                    part is None or uint(part, (1 << 32)-1) if key == 'pid' else
                    part is None or gameid(part) if key == 'gameid64' else
                    part is None or uint(part, 5) if key == 'state' else
                    part is None or uint(part, 16) if key in ('arrayLength', 'heldOccurrences') else
                    isinstance(part, str) and len(part) <= 64)
                if not valid:
                    item['fieldStates'][key] = 'invalid-type-or-range'
                if valid:
                    item['reported'][key] = part
                    if key in enums and part not in enums[key]:
                        item['fieldStates'][key] = 'unsupported-value'
                        item['fieldIssues'].append(key + ':unsupported-value')
                else:
                    item['fieldIssues'].append(key + ':invalid-type-or-range')
            item['unknownFieldCount'] = len(call) - sum(key in call for key in fields)
            selected = item['reported']
            if selected.get('ordinal') != index + 1:
                item['fieldIssues'].append('ordinal:original-order-disagrees')
            if not item['fieldIssues']:
                if (selected['outcome'] == 'threw') != (selected['resultKind'] == 'throw'):
                    item['fieldIssues'].append('outcome-result-kind-disagrees')
                if selected['resultKind'] == 'array':
                    if selected['kind'] != 'list':
                        item['fieldIssues'].append('array-observation-outside-list-call')
                    if selected['arrayLength'] is None or (selected['heldOccurrences'] is not None and selected['heldOccurrences'] > selected['arrayLength']):
                        item['fieldIssues'].append('array-length-held-count-disagrees')
                elif selected['arrayLength'] is not None or selected['heldOccurrences'] is not None:
                    item['fieldIssues'].append('non-array-has-array-observation')
                if selected['kind'] == 'state':
                    if any(selected[key] is not None for key in ('appid', 'pid')) or any(
                            selected[key] != 'not-applicable' for key in ('appidRole', 'pidRole')):
                        item['fieldIssues'].append('state-call-has-lookup-arguments')
                elif selected['gameid64'] is not None or selected['gameidRole'] != 'not-applicable' or selected['state'] is not None:
                    item['fieldIssues'].append('lookup-list-has-state-arguments')
                if selected['kind'] == 'list' and (selected['pid'] is not None or selected['pidRole'] != 'not-applicable'):
                    item['fieldIssues'].append('list-call-has-PID-argument')
            if item['fieldIssues']:
                issues.append('calls[' + str(index) + ']:invalid-observation')
    else:
        issues.append('calls:' + view['callsFieldState'])
    view['unknownFieldCount'] = len(value) - sum(key in value for key in metadata + ('calls',))
    view['state'] = 'unsupported-schema' if not known else 'invalid-fields' if issues else 'reported-original-call-observations'
    return view


def menu_input_trace_evidence(members, current_pid):
    """Project only the original flat Native receipts; never attest delivery.

    Identity is the alias's immutable diagnostic copy, including in a previous
    receipt carried by a new Host. It cannot supply the Native writer's birth.
    """
    snapshots = []
    receipts = missing = malformed_targets = total = unrecognized = 0
    slots = collections.Counter()
    bool_fields = ('diagnosticOnly', 'physicalInputProven', 'sourceEntryObserved',
        'managerEntryObserved', 'documentEventObserved', 'attempted', 'installationComplete',
        'methodCleanupPending', 'listenerCleanupPending', 'history', 'current')
    text_fields = ('scope', 'status', 'observationState', 'reason', 'cleanupState',
        'deliveryEvidence', 'hardwareDelivery', 'zeroCountMeaning')
    number_fields = ('schema', 'eventCount', 'maxEvents', 'windowLimitMs', 'documentCount',
        'methodHookCount', 'listenerCount', 'restoredMethodCount', 'returnedMethodCount',
        'notInstalledMethodCount', 'newerMethodOwnerPreservedCount')
    identity_fields = ('source', 'diagnosticOnly', 'current', 'gamePid', 'gameCreated',
        'steamPid', 'steamCreated', 'hostPid', 'hostCreated', 'browserId',
        'logicalGameid64', 'registeredGameid64', 'registeredAppid')
    count_fields = ('sourceDown', 'sourceUp', 'managerAction', 'managerTarget',
        'targetReadOutsideAction', 'actionSourceGamepad', 'actionSourceKeyboard',
        'actionSourceOther', 'actionSourceUnknown', 'documentVgpDown', 'documentVgpUp',
        'documentTouchStart', 'documentTouchEnd', 'documentPointerDown', 'documentPointerUp',
        'documentMouseDown', 'methodReturned', 'methodThrew', 'foreignThis',
        'targetHeldContext', 'targetOtherContext', 'targetMenuDocument',
        'targetOtherDocument', 'targetUnknown')

    def uint(value):
        return type(value) is int and 0 <= value <= (1 << 53) - 1

    def bounded_scalar(value):
        return (value is None or type(value) is bool or uint(value) or
                (isinstance(value, str) and len(value) <= 256))

    def valid_trace_field(key, value):
        if key in bool_fields:
            return type(value) is bool
        if key in text_fields:
            return isinstance(value, str) and len(value) <= 256
        if key in number_fields:
            return uint(value)
        return (value is None or uint(value) or
                (type(value) is float and math.isfinite(value) and 0 <= value <= (1 << 53) - 1))

    # Counts describe retained receipt rows, including queued rows; no request
    # joining, current-lifetime inference or expectation of trace installation.
    phase_counts, absence_counts = collections.Counter(), collections.Counter()
    absence_by_pid, absence_witnesses = {}, {}

    def observe_absence(filename, line, row, metadata, target):
        phase = row.get('phase')
        if phase == 'queued':
            context = 'reported-queued'
        elif (phase == 'completed' and row.get('status') == 'steam-menu-unavailable'
                and row.get('liveStage') == 'preflight' and isinstance(target, dict) and not target
                and type(row.get('inputSubmitted')) is int and row['inputSubmitted'] == 0
                and all(row.get(key) is False for key in ('keyDelivered', 'mutated', 'menuStateVerified'))):
            context = 'reported-preflight-refusal'
        elif (phase == 'completed' and row.get('status') == 'accepted' and isinstance(target, dict)
                and target.get('isGameOverlay') is False and target.get('runtimeAccepted') is True
                and all(row.get(key) is True for key in ('keyDelivered', 'mutated', 'menuStateVerified'))):
            context = 'reported-non-overlay-state-confirmed'
        elif phase == 'completed':
            context = 'completed-unclassified'
        else:
            context = 'phase-unreported-or-other'
        writer, writer_field = process_identity(row, filename)
        relation = ('writer-or-manifest-unknown' if writer == 'unknown' or current_pid is None else
                    'pid-matches-manifest' if writer == str(current_pid) else 'other-pid')
        absence_counts[context] += 1
        absence_by_pid.setdefault(relation, collections.Counter())[context] += 1
        key = context, relation  # Five fixed contexts x three PID relations, at most15.
        if key in absence_witnesses:
            return
        fields = ('phase', 'status', 'liveStage', 'liveReason', 'actionName', 'requestId',
            'generation', 'powerGeneration', 'inputSubmitted', 'keyDelivered', 'mutated',
            'menuStateVerified', 'sourceCurrent', 'writerProcessCreation100ns')
        reported = {field: row[field] for field in fields if field in row and bounded_scalar(row[field])}
        target_state = ('missing' if 'menuTarget' not in row else 'null' if target is None else
                        'object' if isinstance(target, dict) else 'non-object:' + type(target).__name__)
        moment = stamp(row, filename)
        absence_witnesses[key] = {'file': filename, 'line': line, **metadata,
            'event': event_name(row), 'context': context, 'pidRelation': relation,
            'writerPid': None if writer == 'unknown' else writer, 'writerIdentityField': writer_field,
            'time': moment if isinstance(moment, str) and len(moment) <= 256 else None,
            'menuTargetFieldState': target_state, 'reported': reported,
            'fieldsNotProjected': [field for field in fields if field in row and field not in reported]}

    prefix, suffix = [], collections.deque(maxlen=MAX_MENU_TRACE_SNAPSHOTS)
    first_shapes, active_tails = {}, {}

    def shape_keys(slot, trace, recognized):
        if not recognized:
            return ((slot, 'route-shape', 'outer-trace-unrecognized'),)
        route = trace.get('nativeRoute')
        native_known = (isinstance(route, dict) and type(route.get('schema')) is int and
            route['schema'] == 1 and route.get('diagnosticOnly') is True and
            route.get('scope') == 'existing-alias-original-calls')
        status = route.get('status') if native_known else None
        shape = ('missing' if 'nativeRoute' not in trace else 'null' if route is None else
            'invalid-type' if not isinstance(route, dict) else 'unsupported-schema' if not native_known else
            status if isinstance(status, str) and status in ('not-started', 'observing', 'stopped') else 'unknown-status')
        keys = [(slot, 'route-shape', shape)]
        if native_known:
            calls = route.get('calls')
            outcome = ('missing-or-invalid-calls' if not isinstance(calls, list) else
                'over-capacity-calls' if len(calls) > 16 else 'empty-calls' if not calls else
                'reported-throw' if any(isinstance(call, dict) and call.get('outcome') == 'threw' for call in calls)
                else 'no-reported-throw')
            keys.append((slot, 'call-shape', outcome))
        return keys

    for filename, rows, metadata in members:
        if canonical_log_name(filename) not in ('native-lifecycle.log', 'native-detail.log'):
            continue
        for line, row in rows:
            if event_name(row) not in MENU_TRACE_EVENTS:
                continue
            receipts += 1
            phase = row.get('phase')
            phase_state = ('missing' if 'phase' not in row else 'null' if phase is None else
                phase if phase in ('queued', 'completed') else 'other-string' if isinstance(phase, str) else 'invalid-type')
            phase_counts[phase_state] += 1
            target = row.get('menuTarget')
            if not isinstance(target, dict):
                missing += 1
                malformed_targets += 'menuTarget' in row
                observe_absence(filename, line, row, metadata, target)
                continue
            present = [key for key in ('inputTrace', 'previousInputTrace') if key in target]
            missing += not present
            if not present:
                observe_absence(filename, line, row, metadata, target)
            for slot in present:
                trace = target[slot]
                total += 1
                slots[slot] += 1
                recognized = (isinstance(trace, dict) and type(trace.get('schema')) is int and
                    trace['schema'] == 1 and trace.get('diagnosticOnly') is True and
                    trace.get('scope') == 'qualified-alias-menu-window')
                unrecognized += not recognized
                ref = (total, filename, line, row, metadata, slot, trace, recognized)
                if len(prefix) < MAX_MENU_TRACE_SNAPSHOTS:
                    prefix.append(ref)
                suffix.append(ref)
                for key in shape_keys(slot, trace, recognized):
                    first_shapes.setdefault(key, ref)
                if filename in ('native-lifecycle.log', 'native-detail.log'):
                    active_tails[filename, slot] = ref

    def render_snapshot(ref):
        _, filename, line, row, metadata, slot, trace, recognized = ref
        writer, writer_field = process_identity(row, filename)
        birth = row.get('writerProcessCreation100ns')
        birth_known = (isinstance(birth, str) and len(birth) <= 20 and birth.isascii() and
            birth.isdecimal() and 0 < int(birth) < (1 << 64))
        item = {'file': filename, 'line': line, **metadata, 'event': event_name(row),
            'location': 'menuTarget.' + slot, 'slot': slot,
            'writerPid': None if writer == 'unknown' else writer,
            'writerIdentityField': writer_field,
            'writerProcessCreation100ns': birth if birth_known else None,
            'writerLifetimeKnown': writer != 'unknown' and birth_known,
            'currentPidMatch': None if writer == 'unknown' or current_pid is None else writer == str(current_pid),
            'time': stamp(row, filename), 'recognized': recognized,
            'reported': {}, 'fieldIssues': [], 'fieldsNotProjected': {}}
        snapshots.append(item)
        if not recognized:
            item['fieldIssues'].append({'field': 'menuTarget.' + slot,
                'issue': 'unrecognized-trace-object-or-schema;original-retained'})
            return
        reported, issues = item['reported'], item['fieldIssues']

        def project(source, fields, path, valid):
            output = {}
            for key in fields:
                if key not in source:
                    continue
                value = source[key]
                if valid(key, value):
                    output[key] = value
                else:
                    issues.append({'field': path + key, 'issue': 'unexpected-type-or-budget;original-retained'})
            item['fieldsNotProjected'][path.rstrip('.')] = len(source) - sum(key in source for key in fields)
            return output

        fields = bool_fields + text_fields + number_fields + ('observedWindowMs', 'identity', 'counts', 'nativeRoute')
        reported.update(project(trace, bool_fields + text_fields + number_fields + ('observedWindowMs',), '', valid_trace_field))
        item['fieldsNotProjected']['trace'] = len(trace) - sum(key in trace for key in fields)
        item['fieldsNotProjected'].pop('', None)
        for key, keys, valid in (
            ('identity', identity_fields, lambda key, value: type(value) is bool if key in ('diagnosticOnly', 'current') else bounded_scalar(value) and type(value) is not bool),
            ('counts', count_fields, lambda key, value: uint(value))):
            if key not in trace:
                continue
            if not isinstance(trace[key], dict):
                issues.append({'field': key, 'issue': 'expected-object;original-retained'})
                continue
            reported[key] = project(trace[key], keys, key + '.', valid)
        if 'nativeRoute' in trace:
            reported['nativeRoute'] = native_menu_route_evidence(trace['nativeRoute'])
    if total <= MAX_MENU_TRACE_SNAPSHOTS:
        retained = prefix
    else:
        # At most2slots*(9route+5call shapes)+2exact names*2slots=32
        # fixed witnesses. Fill the original128 cap with balanced edge refs.
        selected = {ref[0]: ref for ref in list(first_shapes.values()) + list(active_tails.values())}
        for head, tail in zip(prefix, reversed(suffix)):
            for ref in (head, tail):
                if len(selected) < MAX_MENU_TRACE_SNAPSHOTS:
                    selected.setdefault(ref[0], ref)
        retained = [selected[key] for key in sorted(selected)]
    for ref in retained:
        render_snapshot(ref)
    return {'schemaVersion': 1, 'scope': 'bounded-retained-Native-menu-receipts;not-input-or-cleanup-attestation',
        'menuReceiptsObserved': receipts, 'menuReceiptsWithoutTrace': missing,
        'nonObjectMenuTargets': malformed_targets, 'traceSnapshotsObserved': total,
        'traceSnapshotsBySlot': dict(slots), 'unrecognizedTraceSnapshots': unrecognized,
        'maxListedSnapshots': MAX_MENU_TRACE_SNAPSHOTS, 'traceSnapshotsNotListed': total - len(snapshots),
        'snapshots': snapshots,
        'snapshotSelection': {
            'policy': 'fixed-first-route-and-call-shapes,last-exact-active-name-tails,then-balanced-encounter-edges',
            'maxFixedWitnesses': 32, 'firstShapeWitnesses': len(first_shapes), 'activeNameTailWitnesses': len(active_tails),
            'allObservedSnapshotSlotsListed': total <= MAX_MENU_TRACE_SNAPSHOTS,
            'countScope': 'valid-object-retained-snapshot-slots;not-unique-aliases-requests-or-full-operation-history',
            'crossFileTimeOrderInferred': False, 'duplicateNameWinnerSelected': False},
        'receiptStageEvidence': {
            'countUnit': 'retained-Native-receipt-rows;not-unique-requests',
            'recordCountsByReportedPhase': dict(phase_counts),
            'absentTraceContexts': dict(absence_counts),
            'absentTraceContextsByPidRelation': {key: dict(value) for key, value in absence_by_pid.items()},
            'absenceWitnesses': list(absence_witnesses.values()), 'maxAbsenceWitnesses': 15,
            'absenceRowsNotListed': missing - len(absence_witnesses),
            'selection': 'first-retained-row-per-fixed-context-and-PID-relation;not-type-or-lifetime-coverage',
            'missingTraceImpliesDeliveryFailure': False, 'pidMatchProvesCurrentLifetime': False,
            'requestPairsOrUniqueCountsInferred': False, 'manifestSuppliesWriterBirth': False,
            'readingRules': [
                'queued and completed rows may describe one request;do not add them as separate attempts',
                'preflight-refusal requires explicit coherent refusal fields;not a trace installation failure',
                'non-overlay-state-confirmed is producer-reported menu state;not in-game or physical input proof',
                'unclassified,missing,null and malformed fields remain unknown;absence alone grants no result',
                'rotations and duplicate archive occurrences are counted as retained rows;never joined or deduplicated',
                'PID relation only compares reported PID;birth is not borrowed from manifest,other rows or trace identity']},
        'readRules': ['inputTrace is carried by this request;its identity copy is diagnostic and never current authority',
            'previousInputTrace retains its own identity and reported history/current flags;never relabel it with the carrier Host',
            'zero or missing observations mean UNKNOWN;source/manager/document observations do not prove hardware delivery',
            'windowLimitMs is checked on existing events,not a hard-wall cleanup timer;retain observedWindowMs and pending cleanup',
            'cleanupState describes trace hooks/listeners,not child/Job/provider/SDK return',
            'snapshots and repeated rotations remain separate;original JSON and omitted fields remain in the payload'],
        'manifestSuppliesWriterBirth': False, 'identityCopySuppliesWriterBirth': False,
        'physicalDeliveryInferred': False, 'resourceReturnInferred': False}


def menu_composition_evidence(members, current_pid):
    """Bounded original composition receipts, never an input or Native ACK."""
    bool_fields = ('diagnosticOnly', 'nativeRequestSubmitted', 'inputCapabilityGranted',
                   'physicalInputProven', 'sourceTupleMatchesSelected', 'browserMountedDuringKey')
    number_fields = ('originalQueueLength', 'selectedQueueLength')
    top_fields = ('schema', 'scope', 'status') + bool_fields + number_fields
    cache_fields = ('scope', 'diagnosticOnly', 'snapshot', 'atomic', 'maxQueueLength', 'mode', 'windowID', 'queue')
    head, tail, types = [], collections.deque(maxlen=32), {}
    receipts = missing = malformed_targets = total = nulls = unrecognized = issue_rows = untracked = 0
    current_writer = pid({'pid': current_pid}, 'native-lifecycle.log')

    def uint(value, maximum):
        return type(value) is int and 0 <= value <= maximum

    def text(value):
        return isinstance(value, str) and len(value) <= 256

    def queue(value):
        return (isinstance(value, list) and len(value) <= 16 and
            all(type(item) is int and -(1 << 31) <= item < (1 << 32) and item != 0 for item in value))

    def valid_top(key, value):
        return (type(value) is bool if key in bool_fields else uint(value, 16) if key in number_fields else
                type(value) is int and value == 1 if key == 'schema' else text(value))

    def valid_cache(key, value):
        if key in ('diagnosticOnly', 'atomic'):
            return type(value) is bool
        if key in ('scope', 'snapshot'):
            return text(value)
        if key == 'maxQueueLength':
            return uint(value, 16)
        if value is None:
            return key in ('mode', 'windowID', 'queue')
        return queue(value) if key == 'queue' else uint(value, 5 if key == 'mode' else (1 << 53) - 1)

    def known(value):
        return (isinstance(value, dict) and type(value.get('schema')) is int and value['schema'] == 1 and
                value.get('scope') == 'same-key-per-browser-composition' and value.get('diagnosticOnly') is False)

    def cache_known(value):
        return (isinstance(value, dict) and value.get('scope') == 'original-js-composition-cache' and
                value.get('diagnosticOnly') is True and value.get('snapshot') == 'before-supplement' and
                value.get('atomic') is False and type(value.get('maxQueueLength')) is int and value['maxQueueLength'] == 16)

    def token(source, key, valid):
        if key not in source:
            return ('missing',)
        value = source[key]
        if not valid(key, value):
            return ('invalid', type(value).__name__)
        return ('value', type(value).__name__, tuple(value) if isinstance(value, list) else value)

    def describe(value):
        if value is None:
            return ('explicit-null',), {'compositionState': 'explicit-null'}, False
        if not known(value):
            return ('unrecognized', type(value).__name__), {'compositionState': 'unrecognized'}, True
        top = tuple(token(value, key, valid_top) for key in top_fields)
        cache = value.get('cachedComposition')
        if 'cachedComposition' not in value:
            cache_state, cached = 'not-recorded', ()
        elif cache is None:
            cache_state, cached = 'explicit-null', ()
        elif isinstance(cache, dict):
            cache_state = 'recognized-cache' if cache_known(cache) else 'unrecognized-cache'
            cached = tuple(token(cache, key, valid_cache) for key in cache_fields)
        else:
            cache_state, cached = 'unrecognized-cache', (('invalid', type(cache).__name__),)
        submitted = value.get('nativeRequestSubmitted')
        submission = ('reported-true' if submitted else 'reported-false') if type(submitted) is bool else (
            'not-recorded' if 'nativeRequestSubmitted' not in value else 'invalid')
        bad = (any(item[0] == 'invalid' for item in top + cached) or cache_state in ('unrecognized-cache', 'explicit-null') or
               any(key in value and value[key] is not False for key in ('inputCapabilityGranted', 'physicalInputProven')))
        return ('recognized-schema1', top, cache_state, cached), {
            'compositionState': 'recognized-schema1', 'reportedStatus': value.get('status') if text(value.get('status')) else None,
            'submissionFlagState': submission, 'cacheState': cache_state}, bad

    for filename, rows, metadata in members:
        if not re.fullmatch(r'native-(?:lifecycle|detail)\.log(?:\.(?:prev|previous|\d+))?', filename):
            continue
        for line, row in rows:
            if event_name(row) not in MENU_TRACE_EVENTS:
                continue
            receipts += 1
            target = row.get('menuTarget')
            if not isinstance(target, dict):
                missing += 1
                malformed_targets += 'menuTarget' in row
                continue
            if 'composition' not in target:
                missing += 1
                continue
            value = target['composition']
            total += 1
            nulls += value is None
            unrecognized += value is not None and not known(value)
            descriptor, description, bad = describe(value)
            issue_rows += bad
            ref = (total, filename, line, row, metadata, value)
            if len(head) < 32:
                head.append(ref)
            else:
                tail.append(ref)
            if descriptor in types:
                item = types[descriptor];item['rows'] += 1;item['last'] = ref
            elif len(types) < 64:
                types[descriptor] = {'index': len(types), 'description': description, 'rows': 1, 'first': ref, 'last': ref}
            else:
                untracked += 1  # Untracked rows, never an estimate of unique types.

    first_types = {item['first'][0] for item in types.values()}
    selected = sorted({ref[0]: ref for ref in head + list(tail) + [item['first'] for item in types.values()]}.values())
    head_ordinals, tail_ordinals = {ref[0] for ref in head}, {ref[0] for ref in tail}
    observations = []
    for ordinal, filename, line, row, metadata, value in selected:
        writer, field = process_identity(row, filename)
        birth = row.get('writerProcessCreation100ns')
        birth_known = (isinstance(birth, str) and 0 < len(birth) <= 20 and birth.isascii() and
                       birth.isdecimal() and 0 < int(birth) < (1 << 64))
        descriptor, description, _ = describe(value)
        item = {'file': filename, 'line': line, **metadata, 'event': event_name(row), 'time': stamp(row, filename),
            'location': 'menuTarget.composition', 'encounterOrdinal': ordinal,
            'writerPid': None if writer == 'unknown' else writer, 'writerIdentityField': field,
            'writerProcessCreation100ns': birth if birth_known else None,
            'writerInstanceReported': writer != 'unknown' and birth_known, 'writerLifetimeVerifiedByReader': False,
            'currentPidMatch': None if 'unknown' in (writer, current_writer) else writer == current_writer,
            'descriptorTypeIndex': types[descriptor]['index'] if descriptor in types else None,
            'selectionReasons': [reason for reason, present in (('head', ordinal in head_ordinals),
                ('first-type', ordinal in first_types), ('tail', ordinal in tail_ordinals)) if present],
            **description, 'reported': None if value is None else {}, 'fieldIssues': [], 'fieldsNotProjected': {},
            'nativeCompletion': 'UNKNOWN', 'physicalDelivery': 'UNKNOWN', 'resourceReturn': 'UNKNOWN'}
        observations.append(item)
        if value is None:
            continue
        if not known(value):
            item['fieldIssues'].append({'field': 'composition', 'issue': 'unrecognized-object-or-schema;original-retained'})
            item['fieldsNotProjected']['composition'] = len(value) if isinstance(value, dict) else None
            continue
        def project(source, keys, path, valid):
            output = {}
            for key in keys:
                if key not in source:
                    continue
                val = source[key]
                if valid(key, val):
                    output[key] = list(val) if isinstance(val, list) else val
                else:
                    item['fieldIssues'].append({'field': path + key, 'issue': 'unexpected-type-or-budget;original-retained'})
            return output
        reported = item['reported']
        reported.update(project(value, top_fields, '', valid_top))
        for key in ('inputCapabilityGranted', 'physicalInputProven'):
            if key in value and value[key] is not False:
                item['fieldIssues'].append({'field': key, 'issue': 'contradicts-producer-contract;not-authority'})
        if 'cachedComposition' in value:
            cache = value['cachedComposition']
            if cache is None:
                reported['cachedComposition'] = None
            elif isinstance(cache, dict):
                cache_reported = project(cache, cache_fields, 'cachedComposition.', valid_cache)
                reported['cachedComposition'] = cache_reported
                item['fieldsNotProjected']['cachedComposition'] = len(cache) - len(cache_reported)
            if not cache_known(cache):
                item['fieldIssues'].append({'field': 'cachedComposition', 'issue': 'unrecognized-cache-contract;original-retained'})
        item['fieldsNotProjected']['composition'] = len(value) - len(reported)

    def reference(ref):
        ordinal, filename, line, _, metadata, _ = ref
        return {'file': filename, 'line': line, **metadata, 'encounterOrdinal': ordinal}
    descriptors = [{'index': item['index'], **item['description'], 'matchingRows': item['rows'],
        'firstReference': reference(item['first']), 'lastReference': reference(item['last'])} for item in types.values()]
    return {'schemaVersion': 1, 'scope': 'retained-original-menu-composition;not-native-completion-or-input-proof',
        'menuReceiptsObserved': receipts, 'menuReceiptsWithoutComposition': missing,
        'nonObjectMenuTargets': malformed_targets, 'compositionSlotsObserved': total,
        'explicitNullObservations': nulls, 'unrecognizedCompositionObservations': unrecognized,
        'maxListedObservations': 128, 'rowsNotListed': total - len(observations), 'observations': observations,
        'selection': 'first-32,last-32,first-witness-for-up-to-64-supported-field-descriptors;encounter-order-only',
        'descriptorTypes': descriptors,
        'typeCoverage': {'scope': 'exact supported producer fields and bounded cache tuple;not unknown fields,operations,writer lifetimes or input types',
            'retainedTypes': len(types), 'maxRetainedTypes': 64, 'descriptorIssueRows': issue_rows,
            'untrackedRows': untracked, 'untrackedRowsAreUniqueTypes': False,
            'retainedSupportedDescriptorCoverageComplete': None if total == 0 else issue_rows == 0 and untracked == 0},
        'nativeCompletionInferred': False, 'physicalDeliveryInferred': False, 'resourceReturnInferred': False,
        'capabilityGrantedByReader': False, 'manifestOrOtherRowsSupplyWriterBirth': False,
        'readingRules': ['nativeRequestSubmitted is set before the original call;true may coexist with throw,Promise uncertainty or ended ownership',
            'status,submission and reported flags never prove Native completion,controller capability,physical input or recovery',
            'cachedComposition is a non-atomic before-supplement JS cache copy;not the current Native queue or applied state',
            'keep exact signed AppID values and queue order;do not force unsigned normalization or substitute full GameID',
            'missing,null,empty queue,false and malformed values stay distinct;unsupported fields remain in the original payload',
            'type grouping only selects bounded witnesses;rotations and duplicate archive occurrences retain separate row references']}


def gyro_power_observation_record(row):
    """Reported sensor consumer stages;no shared-power or sampling verdict."""
    result={'state':'unsupported-event','reported':{},'fieldIssues':[],
        'physicalSleepInferred':False,'samplingFreshnessInferred':False,
        'hostOrGameConsumptionInferred':False,'automaticRecoveryAuthorized':False,
        'sameOperationAcrossRowsInferred':False}
    if not isinstance(row,dict):return result
    event=row.get('event')
    if event not in ('gyro-power-early-pause-completed','gyro-power-command-queued',
            'gyro-power-command-completed','gyro-power-resume-deferred'):return result
    result['event']=event;issues=result['fieldIssues'];out=result['reported'];missing=False
    def read(name,valid):
        nonlocal missing
        if name not in row:issues.append(name+':missing');missing=True;return
        if valid(row[name]):out[name]=row[name]
        else:issues.append(name+':invalid-type-or-value')
    uint=lambda v:type(v) is int and 0<=v<(1<<64)
    boolean=lambda v:type(v) is bool
    if event=='gyro-power-early-pause-completed':
        read('source',lambda v:v=='os-suspend-callback' and type(v) is str)
        read('subscriptionsStopped',boolean);read('generation',uint)
        read('physicalSleepVerified',lambda v:type(v) is bool and v is False)
    elif event=='gyro-power-command-queued':
        read('ticket',uint);read('request',lambda v:type(v) is int and 0<=v<=0xffffffff)
        read('completed',lambda v:type(v) is bool and v is False);read('wakeSignalled',boolean)
    elif event=='gyro-power-command-completed':
        read('ticket',uint);read('request',lambda v:type(v) is int and v in (3,4))
        for key in ('commandSucceeded','subscriptionsStopped','powerAllowsSensors','suspendRequested'):read(key,boolean)
        read('generation',uint);read('physicalSleepVerified',lambda v:type(v) is bool and v is False)
    else:
        read('powerLifecycle',lambda v:type(v) is str and 0<len(v)<=32 and v.isascii() and all(32<=ord(c)<=126 for c in v))
        read('sessionDisplayState',lambda v:type(v) is int and -(1<<31)<=v<(1<<31))
    result['state']='missing-fields' if missing else 'invalid-fields' if issues else 'reported-consumer-stage'
    result['sourcePauseWithReadyEnvelopeObserved']=(result['state']=='reported-consumer-stage' and
        event=='gyro-power-early-pause-completed' and out.get('subscriptionsStopped') is True and row.get('powerLifecycle')=='ready')
    result['readingRules']=['os-suspend-callback names the original callback source;it does not prove physical sleep',
        'queued completed=false and wakeSignalled are submission diagnostics,not terminal completion',
        'commandSucceeded is this consumer receipt,not sample freshness,shared-power success or game consumption',
        'no missing-later-row conclusion,no ticket join across writer/scope,and no automatic restart permission']
    return result


def gyro_power_observation_evidence(members,current_pid):
    first,last,witnesses=[],collections.deque(maxlen=8),{};counts=collections.Counter();events=collections.Counter();carriers=collections.Counter();seen=0
    for filename,rows,metadata in members:
        carrier=canonical_log_name(filename)
        if carrier not in ('native-lifecycle.log','native-detail.log','gyro-motion.log'):continue
        for line,row in rows:
            if not isinstance(row,dict):continue
            view=gyro_power_observation_record(row)
            if view['state']=='unsupported-event':continue
            seen+=1;counts[view['state']]+=1;events[view['event']]+=1;carriers[carrier]+=1
            value=row.get('pid');writer=value if type(value) is int and 0<value<=0xffffffff else None
            birth=row.get('writerProcessCreation100ns');known=(type(birth) is str and 0<len(birth)<=20 and birth.isascii() and birth.isdecimal() and 0<int(birth)<(1<<64))
            observed=stamp(row,filename);safe={}
            if isinstance(metadata,dict):
                value=metadata.get('archiveEntryOccurrence')
                if type(value) is int and 0<=value<(1<<64):safe['archiveEntryOccurrence']=value
                if type(metadata.get('containerOnly')) is bool:safe['containerOnly']=metadata['containerOnly']
            item={'file':filename if len(filename)<=160 else None,'fileNameOmitted':len(filename)>160,
                'originalFileNameCharacters':len(filename),'line':line,**safe,'writerPid':writer,
                'writerProcessCreation100ns':birth if known else None,'writerLifetimeVerified':False,
                'currentPidMatch':None if writer is None or current_pid is None else str(writer)==str(current_pid),
                'time':observed if isinstance(observed,str) and len(observed)<=160 else None,'evidence':view}
            ref=(seen,item)
            if len(first)<8:first.append(ref)
            last.append(ref);key=(view['event'],view['state'],view['sourcePauseWithReadyEnvelopeObserved'])
            if key not in witnesses and len(witnesses)<32:witnesses[key]=ref
    listed=dict(first+list(last)+list(witnesses.values()))
    return {'schema':1,'scope':'retained-gyro-power-consumer-stage-observations;not-sleep-sampling-or-game-verdict',
        'rowsObserved':seen,'countsByState':dict(counts),'countsByEvent':dict(events),'rowsByCarrier':dict(carriers),
        'rows':[listed[k] for k in sorted(listed)],'maxListedRows':48,'rowsNotListed':seen-len(listed),
        'mirrorsDeduplicated':False,'rowsAreUniqueOperations':False,'fullHistoryProven':False,
        'crossWriterRecoveryChainInferred':False,'runtimeCauseOrPhysicalRecoveryProven':False}


def power_leaf_failure_record(row):
    """Original independent observer catch receipts;not shared-power success."""
    result={'state':'unsupported-event','reported':{},'fieldIssues':[],
            'consumerBusinessCompletionProven':False,'automaticReplayAuthorized':False,
            'sharedPowerTransitionSuccessProven':False,'sameOperationAcrossRowsInferred':False}
    if not isinstance(row,dict):return result
    event=row.get('event')
    if event not in ('power-resume-entry-consumer-failed','power-broadcast-consumer-failed'):return result
    result['event']=event;issues=result['fieldIssues'];out=result['reported'];missing=False
    def read(name,valid):
        nonlocal missing
        if name not in row:issues.append(name+':missing');missing=True;return
        if valid(row[name]):out[name]=row[name]
        else:issues.append(name+':invalid-type-or-value')
    read('consumer',lambda v:isinstance(v,str) and 0<len(v)<=96 and v.isascii() and all(32<=ord(c)<=126 for c in v))
    for key in ('businessCompletionProved','automaticReplay'):read(key,lambda v:type(v) is bool and v is False)
    if event=='power-resume-entry-consumer-failed':
        read('work',lambda v:type(v) is int and 0<=v<=3);read('generation',lambda v:type(v) is int and 0<=v<(1<<64))
    else:
        read('code',lambda v:type(v) is int and 0<=v<=0xffffffff);read('receiptSequence',lambda v:type(v) is int and 0<=v<(1<<64))
        if 'receiptSequence' in out:result['sequenceReportedNonzero']=out['receiptSequence']!=0
    result['state']='missing-fields' if missing else 'invalid-fields' if issues else 'reported-consumer-exception'
    result['readingRules']=['code is original Windows power-message code,not HRESULT or Win32 failure',
        'work is original SgWork value0..3 and generation is source-reported;neither grants recovery or replay',
        'receiptSequence can be zero for a nontransition message;nonzero does not prove unique/authenticated operation',
        'the original catch can run after partial work;the next independent observer continues without replaying uncertain work']
    return result


def power_leaf_failure_evidence(members,current_pid):
    first,last,witnesses=[],collections.deque(maxlen=8),{};counts=collections.Counter();events=collections.Counter();seen=0
    for filename,rows,metadata in members:
        if canonical_log_name(filename) not in ('native-lifecycle.log','native-detail.log'):continue
        for line,row in rows:
            if row.get('event') not in ('power-resume-entry-consumer-failed','power-broadcast-consumer-failed'):continue
            seen+=1;view=power_leaf_failure_record(row);counts[view['state']]+=1;events[row['event']]+=1
            writer,field=process_identity(row,filename);birth=row.get('writerProcessCreation100ns')
            known=isinstance(birth,str) and birth.isascii() and birth.isdecimal() and 0<len(birth)<=20 and 0<int(birth)<(1<<64)
            observed=stamp(row,filename);safe_metadata={}
            if isinstance(metadata,dict):
                occurrence=metadata.get('archiveEntryOccurrence');container=metadata.get('containerOnly')
                if type(occurrence) is int and 0<=occurrence<(1<<64):safe_metadata['archiveEntryOccurrence']=occurrence
                if type(container) is bool:safe_metadata['containerOnly']=container
            item={'file':filename if len(filename)<=160 else None,'fileNameOmitted':len(filename)>160,
                'originalFileNameCharacters':len(filename),'line':line,**safe_metadata,'writerPid':None if writer=='unknown' else writer,
                'writerIdentityField':field,'writerProcessCreation100ns':birth if known else None,
                'currentPidMatch':None if writer=='unknown' or current_pid is None else writer==str(current_pid),
                'writerLifetimeVerified':False,'time':observed if observed is None or len(observed)<=160 else None,'evidence':view}
            ref=(seen,item)
            if len(first)<8:first.append(ref)
            last.append(ref);key=(row['event'],view['state'])
            if key not in witnesses and len(witnesses)<32:witnesses[key]=ref
    listed=dict(first+list(last)+list(witnesses.values()))
    return {'schema':1,'scope':'retained-original-power-leaf-catch-receipts;not-power-device-or-recovery-success',
            'rowsObserved':seen,'countsByState':dict(counts),'countsByEvent':dict(events),'rows':[listed[k] for k in sorted(listed)],
            'maxListedRows':48,'rowsNotListed':seen-len(listed),'fullHistoryProven':False,'uniqueOperationsInferred':False,
            'runtimeConsumerCauseOrSharedTransitionSuccessProven':False,'automaticReplayAuthorized':False}


def power_projection_failure_evidence(members, current_pid):
    """Retain the failed dispatch's captured tuple, never recreate a power lease."""
    observations = []
    total = 0
    maximum = 128

    def uint(value, bits):
        return type(value) is int and 0 <= value < (1 << bits)

    def decimal(value):
        return (isinstance(value, str) and 0 < len(value) <= 20 and value.isascii()
                and value.isdecimal() and int(value) < (1 << 64))

    def publication(value):
        return value is None or decimal(value)

    # Select witnesses before copying payloads. Raw IDs are not type keys:
    # this groups diagnostic shapes,not process lifetimes or operations.
    prefix, suffix = [], collections.deque(maxlen=maximum)
    shapes, active_tails = {}, {}
    shape_issue_rows = untracked_rows = 0

    def pid_relation(row, filename):
        writer = pid(row, filename)
        return ('pid-or-manifest-unknown' if writer == 'unknown' or current_pid is None else
                'pid-matches-manifest' if writer == str(current_pid) else 'other-pid')

    def shape_descriptor(row, filename):
        consumer = row.get('consumer')
        good_consumer = isinstance(consumer, str) and 0 < len(consumer) <= 256
        consumer_state = ('recorded' if good_consumer else 'missing' if 'consumer' not in row else
                          'null' if consumer is None else 'overlong-text' if isinstance(consumer, str) else 'invalid-type')
        context = row.get('projectionContext')
        recognized = (isinstance(context, dict) and type(context.get('schemaVersion')) is int
            and context['schemaVersion'] == 1 and context.get('diagnosticOnly') is True and context.get('actionAuthority') is False)
        context_state = ('recognized' if recognized else 'missing' if 'projectionContext' not in row else
                         'null' if context is None else 'unsupported-object' if isinstance(context, dict) else 'invalid-type')
        flag = context.get('capturedPowerReadSucceeded') if recognized else None
        flag_state = ('read-succeeded' if flag is True else 'read-failed' if flag is False else
            'missing' if recognized and 'capturedPowerReadSucceeded' not in context else
            'null' if recognized and flag is None else 'invalid-type' if recognized else 'uninterpreted')
        power = context.get('capturedPower') if recognized else None
        power_state = ('object' if isinstance(power, dict) else 'missing' if recognized and 'capturedPower' not in context else
            'null' if recognized and power is None else 'invalid-type' if recognized else 'uninterpreted')
        bad = not good_consumer or not recognized or type(flag) is not bool
        bad = bad or (flag is True and not isinstance(power, dict)) or (flag is False and power is not None)
        tokens = []
        for key in ('size', 'version', 'phase', 'flags'):
            if isinstance(power, dict):
                value = power.get(key)
                state = ('recorded' if uint(value, 32) else 'missing' if key not in power else 'null' if value is None else 'invalid')
                bad = bad or state != 'recorded'
                tokens.append((key, state, value if state == 'recorded' else None))
            else:
                tokens.append((key, 'uninterpreted', None))
        relation = pid_relation(row, filename)
        key = (consumer_state, consumer if good_consumer else None, context_state, flag_state, power_state, tuple(tokens), relation)
        description = {'consumerState': consumer_state, 'consumer': consumer if good_consumer else None,
            'contextState': context_state, 'captureReadState': flag_state, 'capturedPowerState': power_state,
            'powerShape': tokens,
            'pidRelation': relation}
        return key, description, bool(bad)

    for filename, rows, metadata in members:
        if canonical_log_name(filename) not in ('native-lifecycle.log', 'native-detail.log'):
            continue
        for line, row in rows:
            if event_name(row) != 'power-projection-consumer-failed':
                continue
            total += 1
            ref = (total, filename, line, row, metadata)
            if len(prefix) < maximum:
                prefix.append(ref)
            suffix.append(ref)
            key, description, bad = shape_descriptor(row, filename)
            shape_issue_rows += bad
            if key in shapes:
                shapes[key]['rows'] += 1; shapes[key]['last'] = ref
            elif len(shapes) < 64:
                shapes[key] = {'index': len(shapes), 'description': description, 'rows': 1, 'first': ref, 'last': ref}
            else:
                untracked_rows += 1
            if filename in ('native-lifecycle.log', 'native-detail.log'):
                active_tails[filename, pid_relation(row, filename)] = ref
    first_shapes = {value['first'][0] for value in shapes.values()}
    active_ordinals = {ref[0] for ref in active_tails.values()}
    prefix_ordinals, suffix_ordinals = {ref[0] for ref in prefix}, {ref[0] for ref in suffix}
    if total <= maximum:
        selected = prefix
    else:
        selected_by_ordinal = {ref[0]: ref for ref in [value['first'] for value in shapes.values()] + list(active_tails.values())}
        for head, tail in zip(prefix, reversed(suffix)):
            for ref in (head, tail):
                if len(selected_by_ordinal) < maximum:
                    selected_by_ordinal.setdefault(ref[0], ref)
        selected = [selected_by_ordinal[key] for key in sorted(selected_by_ordinal)]
    for ordinal, filename, line, row, metadata in selected:
        writer, writer_field = process_identity(row, filename)
        birth = row.get('writerProcessCreation100ns')
        birth_known = decimal(birth) and int(birth) > 0
        item = {'file': filename, 'line': line, **metadata, 'event': event_name(row),
            'writerPid': None if writer == 'unknown' else writer, 'writerIdentityField': writer_field,
            'writerProcessCreation100ns': birth if birth_known else None,
            'writerLifetimeKnown': writer != 'unknown' and birth_known,
            'currentPidMatch': None if writer == 'unknown' or current_pid is None else writer == str(current_pid),
            'time': stamp(row, filename), 'reported': {}, 'projectionContext': {},
            'contextStatus': 'not-recorded' if 'projectionContext' not in row else 'unrecognized-context',
            'fieldIssues': []}
        observations.append(item)
        item['encounterOrdinal'] = ordinal
        key, _, _ = shape_descriptor(row, filename)
        item['diagnosticShapeIndex'] = shapes[key]['index'] if key in shapes else None
        item['selectionReasons'] = [reason for reason, present in (
            ('encounter-head', ordinal in prefix_ordinals), ('first-diagnostic-shape', ordinal in first_shapes),
            ('encounter-tail', ordinal in suffix_ordinals), ('active-name-tail', ordinal in active_ordinals)) if present]
        moment = item['time']
        item['timeTextTruncated'] = isinstance(moment, str) and len(moment) > 512
        if item['timeTextTruncated']:
            item['timeOriginalCharacters'] = len(moment); item['time'] = moment[:512]
            item['fieldIssues'].append({'field': 'time', 'issue': 'overlong-string;original-retained'})

        def select(source, fields, prefix, valid):
            selected = {}
            for key in fields:
                if key in source:
                    if valid(key, source[key]):
                        selected[key] = source[key]
                    else:
                        item['fieldIssues'].append({'field': prefix + key,
                            'issue': 'unexpected-type-or-budget;original-retained'})
            return selected

        item['reported'] = select(row, ('consumer', 'observedGeneration', 'powerGeneration',
            'businessCompletionProved', 'automaticBusinessReplay'), '',
            lambda key, value: isinstance(value, str) and len(value) <= 256 if key == 'consumer' else
                type(value) is bool if key in ('businessCompletionProved', 'automaticBusinessReplay') else uint(value, 64))
        context = row.get('projectionContext')
        if context is None and 'projectionContext' not in row:
            continue
        if (not isinstance(context, dict) or type(context.get('schemaVersion')) is not int
                or context['schemaVersion'] != 1 or context.get('diagnosticOnly') is not True
                or context.get('actionAuthority') is not False):
            item['fieldIssues'].append({'field': 'projectionContext', 'issue': 'unrecognized-schema;original-retained'})
            continue
        item['contextStatus'] = 'producer-reported-diagnostic'
        result = select(context, ('schemaVersion', 'diagnosticOnly', 'actionAuthority',
            'publicationRevision', 'capturedPowerReadSucceeded'), 'projectionContext.',
            lambda key, value: publication(value) if key == 'publicationRevision' else
                uint(value, 32) if key == 'schemaVersion' else type(value) is bool)
        item['projectionContext'] = result
        if 'capturedPower' not in context:
            continue
        power = context['capturedPower']
        if power is None:
            result['capturedPower'] = None
        elif isinstance(power, dict):
            result['capturedPower'] = select(power, ('size', 'version', 'generation', 'publicationRevision',
                'observedNotificationSequence', 'phase', 'flags', 'suspendSerial'), 'projectionContext.capturedPower.',
                lambda key, value: uint(value, 32) if key in ('size', 'version', 'phase', 'flags') else decimal(value))
        else:
            item['fieldIssues'].append({'field': 'projectionContext.capturedPower', 'issue': 'expected-object-or-null;original-retained'})
        if context.get('capturedPowerReadSucceeded') is False and power is not None:
            item['fieldIssues'].append({'field': 'projectionContext.capturedPower', 'issue': 'capture-failed-with-payload;not-authority'})
    def reference(ref):
        ordinal, filename, line, _, metadata = ref
        return {'file': filename, 'line': line, **metadata, 'encounterOrdinal': ordinal}
    descriptors = [{'index': value['index'], **value['description'], 'matchingRows': value['rows'],
        'firstEncounterOrdinal': value['first'][0], 'lastEncounterOrdinal': value['last'][0]} for value in shapes.values()]
    coverage = None if total == 0 else untracked_rows == 0 and shape_issue_rows == 0
    return {'schemaVersion': 2, 'scope': 'failed-original-projection-diagnostics;not-admission-or-resource-return-proof',
        'rowsObserved': total, 'maxListedObservations': maximum, 'rowsNotListed': total - len(observations),
        'observations': observations, 'captureAndObservedClocksMerged': False,
        'selection': {'policy': 'first-diagnostic-shapes,last-exact-active-name-PID-relation-tails,then-balanced-encounter-edges',
            'maxShapeWitnesses': 64, 'maxActiveTailWitnesses': 6,
            'activeTailReferences': [reference(ref) for ref in active_tails.values()],
            'allObservedRowsListed': total <= maximum, 'crossFileTimeOrderInferred': False,
            'manifestPidRelationProvesCurrentLifetime': False, 'duplicateNameWinnerSelected': False},
        'descriptorTypes': descriptors,
        'typeCoverage': {'scope': 'bounded-consumer/capture-shapes-and-PID-relations-in-retained-rows;not-operations-or-full-source-history',
            'status': 'no-matching-rows' if total == 0 else 'complete-for-retained-shapes' if coverage else 'incomplete',
            'retainedTypes': len(shapes), 'maxRetainedTypes': 64, 'shapeIssueRows': shape_issue_rows,
            'untrackedRows': untracked_rows, 'untrackedRowsAreUniqueTypes': False, 'descriptorCoverageComplete': coverage},
        'writerBirthInferredFromCapturedPower': False, 'permissionInferred': False, 'resourceReturnInferred': False,
        'readingRules': ['capturedPower is the tuple already held before the callback;observedGeneration is read by the failure logger later',
            'publicationRevision names the dispatch,not a new sleep or retry allowance;retain notification/serial/phase/flags separately',
            'capturedPowerReadSucceeded is read success only,not kKnown,Ready,hardware admission or a live lease',
            'null or missing tuple stays unknown;do not substitute manifest,export snapshot or a later request',
            'same lifecycle-publication retries can be detailed/suppressed;missing repeats do not prove recovery',
            'first shapes and active-name tails fit128 rows;encounter order is not time;PID relation is not lifetime or a duplicate-name winner',
            'powerShape=[field,state,value];ordinals index rows;descriptor/overflow counts are shapes/rows,not operations']}


def session_marker_diagnostic_exception_evidence(availability,current_pid):
    result={'state':'missing','writerPid':None,'writerProcessCreation100ns':None,'reportedWriterTupleComplete':False,
        'writerIdentityProven':False,'currentPidMatch':None,'reported':{},'rows':[],'fieldIssues':[],
        'countsSummedWithNativeFileFailures':False,'fileWriteFailureProven':False,'completeHistoryOrRetirementProven':False}
    availability=availability if isinstance(availability,dict) else {}
    if 'sessionMarkerDiagnosticExceptionEvidence' not in availability:return result
    value=availability['sessionMarkerDiagnosticExceptionEvidence']
    if not isinstance(value,dict):result['state']='null' if value is None else 'invalid-type';return result
    if value.get('scope')!='writer-context-borrowed-from-original-nativeLogFailureEvidence;not-independent-identity-or-delivery':result['state']='unsupported-writer-scope';return result
    issues=result['fieldIssues'];pid=value.get('writerPid');birth=value.get('writerProcessCreation100ns')
    if type(pid) is int and 0<pid<(1<<32):result['writerPid']=pid;result['currentPidMatch']=None if current_pid is None else str(pid)==str(current_pid)
    else:issues.append('writerPid:missing-or-invalid')
    if isinstance(birth,str) and 0<len(birth)<=20 and birth.isascii() and birth.isdecimal() and 0<int(birth)<(1<<64):result['writerProcessCreation100ns']=str(int(birth))
    elif birth is not None:issues.append('writerProcessCreation100ns:invalid')
    result['reportedWriterTupleComplete']=result['writerPid'] is not None and result['writerProcessCreation100ns'] is not None
    stats=value.get('stats')
    if not isinstance(stats,dict):result['state']='stats-unrecognized';return result
    if type(stats.get('schemaVersion')) is not int or stats['schemaVersion']!=1:result['state']='unsupported-stats-schema';return result
    if stats.get('snapshotKnown') is False and stats.get('reason')=='contended':result['state']='snapshot-contended';return result
    expected={'snapshotKnown':True,'scope':'session-marker-optional-log-call-catches;argument-preparation-or-propagated-sink;independent-of-file-failure-buckets',
        'maxRows':2,'counterLimit':(1<<64)-1,'flagsAtomicWithCounts':False,'countsAreUniqueLostRows':False,'zeroProvesCompleteHistory':False,
        'fileWriteFailureProven':False,'markerWriteOutcomeProvenByCounts':False,'processHostOrDeviceRetirementProven':False}
    for key,wanted in expected.items():
        part=stats.get(key)
        if type(part) is not type(wanted) or part!=wanted:issues.append(key+':missing-or-unsupported')
        else:result['reported'][key]=part
    for key in ['recordingContentionObserved','recordingExceptionObserved']:
        part=stats.get(key)
        if type(part) is bool:result['reported'][key]=part
        else:issues.append(key+':missing-or-invalid')
    rows=stats.get('rows')
    if not isinstance(rows,list):result['state']='rows-unrecognized';return result
    if len(rows)!=2:issues.append('row-count-not-two')
    seen=set()
    for row in rows[:2]:
        item={'state':'invalid-type'};result['rows'].append(item)
        if not isinstance(row,dict):continue
        event=row.get('event')
        if event not in ('session-clean-exit','session-clean-exit-failed'):item['state']='unsupported-event';continue
        item.update(state='reported',event=event)
        if event in seen:item['state']='duplicate-event';issues.append('rows:duplicate-event')
        seen.add(event);count=row.get('observedExceptions');saturated=row.get('counterLimitReached')
        if type(count) is not int or not 0<=count<(1<<64) or type(saturated) is not bool or saturated!=(count==(1<<64)-1):item['state']='invalid-fields'
        else:item.update(observedExceptions=count,counterLimitReached=saturated)
    if len(seen)!=2:issues.append('rows:two-distinct-events-required')
    result['state']='reported-caller-boundary-exceptions' if not issues and all(row['state']=='reported' for row in result['rows']) else 'reported-with-issues'
    return result


def session_marker_record(row):
    event=row.get('event')
    if event not in ('session-clean-exit','session-clean-exit-failed'):return {'state':'not-applicable'}
    result={'state':'legacy-event-only;marker-outcome-unconfirmed','event':event,'reported':{},'hostOrProcessOrDeviceRetirementProven':False,'fieldIssues':[]}
    if not any(k in row for k in ('schemaVersion','scope','sessionMarkerWriteSucceeded')):return result
    if type(row.get('schemaVersion')) is not int or row['schemaVersion']!=1:result['state']='unsupported-schema';return result
    if row.get('scope')!='session-marker-file-write-result;not-host-process-or-device-retirement':result['state']='unsupported-scope';return result
    for key in ('sessionMarkerWriteSucceeded','nativeProcessExitProven','inputHostRetirementProven','deviceReturnProven'):
        value=row.get(key)
        if type(value) is not bool:result['fieldIssues'].append(key+':missing-or-invalid')
        elif key!='sessionMarkerWriteSucceeded' and value:result['fieldIssues'].append(key+':unexpected-retirement-claim')
        else:result['reported'][key]=value
    if 'sessionMarkerWriteSucceeded' in result['reported'] and result['reported']['sessionMarkerWriteSucceeded']!=(event=='session-clean-exit'):result['fieldIssues'].append('event-outcome-conflict')
    result['state']='invalid-fields' if result['fieldIssues'] else 'reported-marker-write-only'
    return result


def session_marker_evidence(members,current_pid):
    head=[];tail=collections.deque(maxlen=16);first={};counts=collections.Counter();encounter=0
    current=current_pid if type(current_pid) is int else int(current_pid) if isinstance(current_pid,str) and re.fullmatch(r'[0-9]{1,10}',current_pid) else None
    if current is not None and not 0<current<(1<<32):current=None
    for filename,rows,metadata in members:
        if not re.fullmatch(r'native-(?:lifecycle|detail)\.log(?:\.(?:prev|previous|\d+))?',filename):continue
        for line,row in rows:
            value=session_marker_record(row)
            if value['state']=='not-applicable':continue
            encounter+=1;counts[value['state']]+=1
            pid=row.get('pid');pid=pid if type(pid) is int and 0<pid<(1<<32) else None
            item={'encounter':encounter,'file':filename,'line':line,'pid':pid,'writerLifetimeProven':False,'pidMatchesCurrent':pid is not None and pid==current,'record':value}
            if len(head)<16:head.append(item)
            else:tail.append(item)
            key=(value['state'],value['event'],value['reported'].get('sessionMarkerWriteSucceeded'))
            if key not in first and len(first)<16:first[key]=item
    listed={q['encounter']:q for q in head+list(tail)+list(first.values())}
    return {'rowsObserved':encounter,'rowsByState':dict(counts),'observations':[listed[k] for k in sorted(listed)],'rowsNotListed':encounter-len(listed),'markerBoundsProcessLifetime':False,'processHostOrDeviceRetirementProven':False,'uniqueOperationsInferred':False,'scope':'retained-native-session-marker-rows-only;not-live-process-or-resource-retirement'}


def local_retirement_record(row):
    """Only coherent named local facts; never whole-Host/device return."""
    event = row.get('event')
    primary = event == 'input-host-primary-thread-retirement'
    process = event == 'input-host-process-retirement'
    if primary or process:
        required = (('handleClosed', 'closeError', 'ownerRetained', 'scope') if primary else
            ('waitState', 'waitError', 'hostExitConfirmed', 'closeAttempted', 'handleClosed',
             'closeError', 'hostHandleRetained', 'scope'))
    elif event == 'input-host-session-retained':
        required = ('hostHandleRetained', 'pipeRetirementProven', 'primaryThreadRetirementProven',
                    'helloHistoryRetained', 'targetHistoryRetained', 'targetCreationPossible',
                    'quarantined', 'publishing', 'reason')
    elif event == 'input-host-session-local-retired':
        required = ('pipeRetirementProven', 'primaryThreadRetirementProven', 'tupleClearPending', 'scope')
    else:
        return 'not-applicable'
    if any(key not in row for key in required):
        return 'not-recorded-or-incomplete'
    uint = lambda v: type(v) is int and 0 <= v < (1 << 32)
    for key in required:
        if key in ('closeError', 'waitState', 'waitError'):
            if not uint(row[key]):return 'inconsistent-or-unrecognized'
        elif key not in ('scope', 'reason') and type(row[key]) is not bool:
            return 'inconsistent-or-unrecognized'
    if primary:
        if row['scope'] != 'creation-thread-handle-only;not-thread-exit-target-or-physical-return':
            return 'inconsistent-or-unrecognized'
        if row['handleClosed'] is True and row['closeError'] == 0 and row['ownerRetained'] is False:
            return 'reported-local-handle-closed'
        if row['handleClosed'] is False and row['closeError'] > 0 and row['ownerRetained'] is True:
            return 'reported-local-close-failed'
        return 'inconsistent-or-unrecognized'
    if process:
        if row['scope'] not in ('process-handle-only; endpoint-and-physical-return-independent',
                                'process-handle-only;endpoint-and-physical-return-separate'):
            return 'inconsistent-or-unrecognized'
        waiting = not row['hostExitConfirmed'] and not row['closeAttempted'] and not row['handleClosed'] and row['closeError'] == 0 and row['hostHandleRetained']
        if waiting and row['waitState'] == 258 and row['waitError'] == 0:
            return 'reported-exit-wait-pending'
        if waiting and row['waitState'] == (1 << 32) - 1 and row['waitError'] > 0:
            return 'reported-exit-wait-failed'
        exited = row['waitState'] == 0 and row['waitError'] == 0 and row['hostExitConfirmed'] and row['closeAttempted']
        if exited and row['handleClosed'] and row['closeError'] == 0 and not row['hostHandleRetained']:
            return 'reported-local-handle-closed'
        if exited and not row['handleClosed'] and row['closeError'] > 0 and row['hostHandleRetained']:
            return 'reported-local-close-failed'
        return 'inconsistent-or-unrecognized'
    if event == 'input-host-session-retained':
        if row['reason'] != 'exit-or-target-return-not-yet-closed' or row['publishing']:
            return 'inconsistent-or-unrecognized'
        if 'repeatObservation' in row and type(row['repeatObservation']) is not bool:
            return 'inconsistent-or-unrecognized'
        held = row['hostHandleRetained'] or row['quarantined'] or not row['pipeRetirementProven'] or not row['primaryThreadRetirementProven']
        return 'reported-session-retained' if held else 'inconsistent-or-unrecognized'
    if (row['scope'] == 'local-handle-retirement-only' and row['pipeRetirementProven'] and
            row['primaryThreadRetirementProven'] and row['tupleClearPending']):
        return 'reported-local-retirement-before-tuple-clear'
    return 'inconsistent-or-unrecognized'


def power_consumer_boundary_evidence(members, current_pid):
    """Keep bounded first-type witnesses as well as retained prefix/suffix rows."""
    events = ('power-resume-consumer-failed', 'power-input-step-failed', 'gyro-power-sta-recovery', 'input-capture-exit-provider-retry',
        'input-host-primary-thread-retirement', 'input-host-process-retirement',
        'input-host-session-retained', 'input-host-session-local-retired')
    head, tail, types = [], collections.deque(maxlen=32), {}
    counts = collections.Counter()
    encounter = untracked_rows = descriptor_issue_rows = 0

    def provider_return_state(row):
        known, value, origin = row.get('resourceReturnedKnown'), row.get('resourceReturned'), row.get('resourceObservation')
        if known is True and type(value) is bool and origin == 'original-final-provider-debt-guard':
            return 'observed-provider-returned' if value else 'observed-provider-owed'
        if known is False and 'resourceReturned' in row and value is None and origin == 'original-final-provider-debt-guard-short-circuited':
            return 'not-observed-short-circuit'
        if 'resourceReturnedKnown' not in row and type(value) is bool:
            return 'legacy-true-origin-unknown' if value else 'legacy-false-origin-unknown'
        return 'inconsistent-or-unrecognized'

    def descriptor(row, event):
        if event in events[4:]:
            state = local_retirement_record(row)
            field = 'phase' if event in events[4:6] else 'reason' if event == events[6] else 'scope'
            value = row.get(field)
            missing_process_phase = event == events[5] and field not in row
            valid = isinstance(value, str) and 0 < len(value) <= 256
            status = 'not-labelled-process-boundary' if missing_process_phase else 'recorded' if valid else 'missing' if field not in row else 'invalid'
            token = (status, value if valid else None)
            bad = state in ('not-recorded-or-incomplete', 'inconsistent-or-unrecognized') or (not valid and not missing_process_phase)
            return (event, token, state), {'event':event, 'discriminatorField':field,
                'discriminatorStatus':status, 'discriminatorValue':value if valid else None,
                'commandAdmissionState':'not-applicable', 'localRetirementState':state}, bad
        field = 'consumer' if event == events[0] else 'step' if event == events[1] else 'provider' if event == events[3] else 'phase'
        value = row.get(field)
        valid = isinstance(value, str) and 0 < len(value) <= 256
        status = 'recorded' if valid else 'missing' if field not in row else 'invalid'
        token = (status, value if valid else type(value).__name__ if status == 'invalid' else None)
        admission = 'not-applicable'
        bad = not valid
        if event == events[2] and value == 'command-retry-returned':
            accepted = row.get('commandSubmissionAccepted')
            admission = ('accepted' if accepted else 'rejected') if type(accepted) is bool else (
                'not-recorded' if 'commandSubmissionAccepted' not in row else 'invalid')
            bad = bad or admission in ('not-recorded', 'invalid')
        elif 'commandSubmissionAccepted' in row:
            bad = True
        provider_state = provider_return_state(row) if event == events[3] else None
        if provider_state == 'inconsistent-or-unrecognized':
            bad = True
        identity = (event, token, admission) if provider_state is None else (event, token, admission, provider_state)
        return identity, {
            'event': event, 'discriminatorField': field, 'discriminatorStatus': status,
            'discriminatorValue': value if valid else None, 'commandAdmissionState': admission,
            **({'providerReturnState': provider_state} if provider_state is not None else {})}, bad

    for filename, rows, metadata in members:
        if not re.fullmatch(r'native-(?:lifecycle|detail)\.log(?:\.(?:prev|previous|\d+))?', filename):
            continue
        for line, row in rows:
            event = row.get('event')
            if not isinstance(event, str) or event not in events:
                continue
            encounter += 1
            counts[event] += 1
            ref = (encounter, filename, line, row, metadata)
            if len(head) < 32:
                head.append(ref)
            else:
                tail.append(ref)
            key, description, bad = descriptor(row, event)
            descriptor_issue_rows += bad
            if key in types:
                item = types[key]
                item['rows'] += 1
                item['last'] = ref
            elif len(types) < 64:
                types[key] = {'index': len(types), 'description': description, 'rows': 1, 'first': ref, 'last': ref}
            else:
                untracked_rows += 1  # Rows, not an exact count of unseen types.
    # Ordinals deduplicate selection roles only. Identical payloads in separate
    # archive occurrences/rotations remain separate observations.
    first_types = {item['first'][0] for item in types.values()}
    selected = sorted({ref[0]: ref for ref in head + list(tail) +
        [item['first'] for item in types.values()]}.values(), key=lambda ref: ref[0])
    head_ordinals, tail_ordinals = {ref[0] for ref in head}, {ref[0] for ref in tail}
    observations = []
    common = ('powerGeneration', 'producerPowerGeneration', 'observedPowerGeneration', 'powerLifecycle')
    fields = {
        events[0]: ('generation', 'consumer', 'dispatchReturned', 'businessCompletionProved', 'automaticReplay'),
        events[1]: ('generation', 'step', 'businessCompletionProved', 'automaticReplay'),
        events[2]: ('phase', 'ownerSerial', 'captureGeneration', 'notificationSequence', 'failedCommandTicket',
                    'observationDeadlineUptimeMs', 'physicalSleepVerified', 'hardwareReady', 'commandSubmissionAccepted'),
        events[3]: ('provider', 'stopReturned', 'attemptBudgetSpent', 'resourceReturnedKnown', 'resourceReturned', 'resourceObservation'),
        events[4]: ('phase', 'runId', 'hostInstanceId', 'epoch', 'handleClosed', 'closeError', 'ownerRetained', 'scope'),
        events[5]: ('phase', 'runId', 'epoch', 'waitState', 'waitError', 'hostExitConfirmed', 'closeAttempted', 'handleClosed', 'closeError', 'hostHandleRetained', 'scope'),
        events[6]: ('runId', 'hostInstanceId', 'epoch', 'hostHandleRetained', 'pipeRetirementProven', 'primaryThreadRetirementProven',
                    'helloHistoryRetained', 'targetHistoryRetained', 'targetCreationPossible', 'quarantined', 'publishing', 'reason', 'repeatObservation'),
        events[7]: ('runId', 'hostInstanceId', 'epoch', 'commandSequence', 'pipeRetirementProven', 'primaryThreadRetirementProven', 'tupleClearPending', 'scope'),
    }
    text_fields = ('powerLifecycle', 'consumer', 'step', 'phase', 'provider', 'resourceObservation', 'runId', 'hostInstanceId', 'scope', 'reason')
    bool_fields = ('dispatchReturned', 'businessCompletionProved', 'automaticReplay', 'physicalSleepVerified',
                   'hardwareReady', 'commandSubmissionAccepted', 'stopReturned', 'attemptBudgetSpent', 'resourceReturnedKnown', 'resourceReturned', 'handleClosed', 'ownerRetained', 'hostExitConfirmed', 'closeAttempted',
                   'hostHandleRetained', 'pipeRetirementProven', 'primaryThreadRetirementProven', 'helloHistoryRetained', 'targetHistoryRetained',
                   'targetCreationPossible', 'quarantined', 'publishing', 'repeatObservation', 'tupleClearPending')
    def decimal(value):
        return (isinstance(value, str) and 0 < len(value) <= 20 and value.isascii() and
                value.isdecimal() and int(value) < (1 << 64))
    for ordinal, filename, line, row, metadata in selected:
        event = row['event']
        writer, writer_field = process_identity(row, filename)
        birth = row.get('writerProcessCreation100ns')
        known_birth = decimal(birth) and int(birth) > 0
        item = {'file': filename, 'line': line, **metadata, 'event': event,
            'writerPid': None if writer == 'unknown' else writer, 'writerIdentityField': writer_field,
            'writerProcessCreation100ns': birth if known_birth else None,
            'writerLifetimeKnown': writer != 'unknown' and known_birth,
            'currentPidMatch': None if writer == 'unknown' or current_pid is None else writer == str(current_pid),
            'time': stamp(row, filename), 'reported': {}, 'fieldIssues': []}
        key, _, _ = descriptor(row, event)
        item['encounterOrdinal'] = ordinal
        item['descriptorTypeIndex'] = types[key]['index'] if key in types else None
        item['selectionReasons'] = [reason for reason, present in (
            ('head', ordinal in head_ordinals), ('first-type', ordinal in first_types),
            ('tail', ordinal in tail_ordinals)) if present]
        observations.append(item)
        for key in common + fields[event]:
            if key not in row:
                continue
            value = row[key]
            if event == events[3] and key == 'resourceReturned' and value is None:
                item['reported'][key] = None
                continue
            valid = (isinstance(value, str) and len(value) <= 256 if key in text_fields else
                type(value) is bool if key in bool_fields else
                type(value) is int and 0 <= value < (1 << 32) if key in ('closeError', 'waitError', 'waitState') else
                (type(value) is int and 0 <= value < (1 << 64)) or decimal(value))
            if valid:
                item['reported'][key] = value
            else:
                item['fieldIssues'].append({'field': key, 'issue': 'unexpected-type-or-budget;original-retained'})
        if event in events[4:]:
            item['localRetirementState'] = local_retirement_record(row)
            if item['localRetirementState'] in ('not-recorded-or-incomplete', 'inconsistent-or-unrecognized'):
                item['fieldIssues'].append({'field':'localRetirementBoundary', 'issue':'missing-inconsistent-or-unrecognized-local-fields'})
        if event == events[3]:
            item['providerReturnState'] = provider_return_state(row)
            if item['providerReturnState'] == 'inconsistent-or-unrecognized':
                item['fieldIssues'].append({'field': 'providerReturnBoundary', 'issue': 'missing-inconsistent-or-unknown-provenance'})
        if 'commandSubmissionAccepted' in row and (event != events[2] or row.get('phase') != 'command-retry-returned'):
            item['fieldIssues'].append({'field': 'commandSubmissionAccepted',
                'issue': 'outside-original-command-return-boundary;meaning-unknown'})
    total = sum(counts.values())
    def reference(ref):
        ordinal, filename, line, _, metadata = ref
        return {'file': filename, 'line': line, **metadata, 'encounterOrdinal': ordinal}
    descriptor_types = [{'index': value['index'], **value['description'], 'matchingRows': value['rows'],
        'firstReference': reference(value['first']), 'lastReference': reference(value['last'])}
        for value in types.values()]
    complete = None if total == 0 else untracked_rows == 0 and descriptor_issue_rows == 0
    return {'schemaVersion': 2, 'scope': 'retained-original-consumer-boundaries;not-action-or-resource-completion',
        'rowsObserved': total, 'rowsByEvent': dict(counts), 'maxListedObservations': 128,
        'rowsNotListed': total - len(observations), 'observations': observations,
        'selection': 'first-32,last-32,and-first-witness-for-up-to-64-descriptor-types;encounter-order-only',
        'descriptorTypes': descriptor_types,
        'typeCoverage': {'scope': 'event/discriminator/admission-shapes-in-supplied-retained-rows;not-operations-or-hardware-input-types',
            'status': 'no-matching-rows' if total == 0 else 'complete-for-retained-descriptors' if complete else 'incomplete',
            'retainedTypes': len(types), 'maxRetainedTypes': 64, 'descriptorIssueRows': descriptor_issue_rows,
            'untrackedRows': untracked_rows, 'untrackedRowsAreUniqueTypes': False,
            'descriptorCoverageComplete': complete},
        'permissionInferred': False, 'recoverySuccessInferred': False, 'resourceReturnInferred': False,
        'missingReturnInferredAsFailure': False, 'writerBirthBackfilled': False,
        'readingRules': [
            'generation/producerPowerGeneration belong to the original attempt;Native powerGeneration is a later envelope observation',
            'provider exit retry stopReturned is not cleanup;new resourceReturned is only the original guard observation;short-circuited null and legacy unknown origin never become failure or whole-resource return',
            'issued precedes the call;commandSubmissionAccepted is the original mailbox admission,not signal,execution or completion',
            'resume-call-returned only reports the original void call returned;it can include early returns',
            'missing return or commandSubmissionAccepted stays unknown;never use manifest,other rows or phase words to fill it',
            'observationDeadlineUptimeMs is absolute Windows boot uptime;not elapsed time,wall time or CPU cost',
            'ownerSerial,captureGeneration,notificationSequence and failedCommandTicket are distinct identities,not process IDs',
            'primary/process handle close,wait pending or wait failure,retained session,and pre-tuple-clear local boundary stay distinct;none proves SDK/physical return',
            'local failure uses coherent exact booleans and uint32 error fields;missing or contradictory fields remain unknown,not success or failure',
            'first/last and bounded first-type witnesses keep their own references;omitted middle rows prevent complete operation-history claims',
            'descriptor types group event plus original consumer/step/phase and typed admission only;not process lifetime or operation identity',
            'type capacity overflow counts untracked rows,not unique types;malformed/missing descriptor fields keep coverage incomplete']}



MODULE_PROFILE_PREPARATION_STATUSES = (
    'not-attempted', 'module-profile-ready', 'binding-unavailable', 'current-rejected',
    'steam-process-unavailable', 'module-unavailable', 'profile-unavailable',
    'module-or-process-changed', 'preparation-exception',
    'preparation-not-requested', 'preparation-busy',
)


def module_profile_preparation_evidence(members, current_pid):
    """Bounded, separate session facts terminals;never recovery or live authority."""
    expected_scope = 'session-module-profile-facts-only'
    head, tail, witnesses = [], collections.deque(maxlen=16), {}
    counts = {key: collections.Counter() for key in ('status', 'scope', 'source')}
    total = recognized_rows = 0

    def text_state(row, key):
        if key not in row:
            return 'missing'
        value = row[key]
        if value is None:
            return 'null'
        if not isinstance(value, str):
            return 'invalid-type'
        if not value:
            return 'empty-string'
        if len(value) > 256:
            return 'overlong-string'
        if key == 'status':
            return value if value in MODULE_PROFILE_PREPARATION_STATUSES else 'other-string'
        if key == 'scope':
            return 'recognized' if value == expected_scope else 'other-string'
        return 'recorded'

    for filename, rows, metadata in members:
        if not re.fullmatch(r'native-(?:lifecycle|detail)\.log(?:\.(?:prev|previous|\d+))?', filename):
            continue
        for line, row in rows:
            if row.get('event') != 'steam-module-profile-preparation':
                continue
            total += 1
            states = {key: text_state(row, key) for key in counts}
            recognized_rows += (states['scope'] == 'recognized' and
                states['status'] in MODULE_PROFILE_PREPARATION_STATUSES and states['source'] == 'recorded')
            ref = (total, filename, line, row, metadata)
            if len(head) < 16:
                head.append(ref)
            else:
                tail.append(ref)
            for key, state in states.items():
                counts[key][state] += 1
                witnesses.setdefault((key, state), ref)
    # Fixed categories only:11+6 status,1+6 scope,1+5 source witnesses.
    # Ordinals merge selection roles,not identical records or ZIP occurrences.
    max_listed = 16 + 16 + len(MODULE_PROFILE_PREPARATION_STATUSES) + 6 + 7 + 6
    selected = sorted({ref[0]: ref for ref in head + list(tail) + list(witnesses.values())}.values(),
        key=lambda ref: ref[0])
    head_ids, tail_ids = {ref[0] for ref in head}, {ref[0] for ref in tail}
    witness_ids = {ref[0] for ref in witnesses.values()}
    observations = []
    for ordinal, filename, line, row, metadata in selected:
        writer, writer_field = process_identity(row, filename)
        birth = row.get('writerProcessCreation100ns')
        birth_known = (isinstance(birth, str) and 0 < len(birth) <= 20 and
            birth.isascii() and birth.isdecimal() and 0 < int(birth) < (1 << 64))
        birth_state = ('missing' if 'writerProcessCreation100ns' not in row else
            'null' if birth is None else 'recorded' if birth_known else
            'invalid-value' if isinstance(birth, str) else 'invalid-type')
        reported, issues, lengths = {}, [], {}
        states = {key: text_state(row, key) for key in counts}
        for key, state in states.items():
            if key in row and (row[key] is None or isinstance(row[key], str)):
                reported[key] = row[key][:256] if isinstance(row[key], str) else None
            if state == 'overlong-string':
                lengths[key] = len(row[key])
            if state in ('missing', 'null', 'invalid-type', 'empty-string', 'overlong-string', 'other-string'):
                issues.append({'field': key, 'issue': state + ';original-retained'})
        time_value = stamp(row, filename)
        if time_value is not None and len(time_value) > 512:
            lengths['time'] = len(time_value)
            issues.append({'field': 'time', 'issue': 'overlong-string;original-retained'})
        observations.append({'file': filename, 'line': line, **metadata,
            'encounterOrdinal': ordinal, 'event': 'steam-module-profile-preparation',
            'selectionReasons': [reason for reason, present in (('head', ordinal in head_ids),
                ('tail', ordinal in tail_ids), ('first-category', ordinal in witness_ids)) if present],
            'writerPid': None if writer == 'unknown' else writer, 'writerIdentityField': writer_field,
            'writerProcessCreation100ns': birth if birth_known else None, 'writerBirthState': birth_state,
            'writerLifetimeKnown': writer != 'unknown' and birth_known,
            'currentPidMatch': None if writer == 'unknown' or current_pid is None else writer == str(current_pid),
            'currentLifetimeProven': False, 'time': time_value[:512] if time_value is not None else None,
            'reported': reported, 'fieldStates': states, 'fieldIssues': issues,
            'originalStringLengths': lengths,
            'recognizedSessionTerminal': states['scope'] == 'recognized' and
                states['status'] in MODULE_PROFILE_PREPARATION_STATUSES and states['source'] == 'recorded'})
    return {'schemaVersion': 1, 'scope': 'bounded-retained-session-module-profile-terminals;not-recovery-or-live-authority',
        'rowsObserved': total, 'recognizedRows': recognized_rows, 'unrecognizedRows': total - recognized_rows,
        'countsByReportedStatus': dict(counts['status']), 'countsByScopeState': dict(counts['scope']),
        'countsBySourceState': dict(counts['source']), 'maxListedObservations': max_listed,
        'rowsNotListed': total - len(observations), 'observations': observations,
        'countUnit': 'retained-rows;not-unique-requests-or-preparation-attempts',
        'selection': 'first-16,last-16,and-first-fixed-field-category-witness;encounter-order-only',
        'missingEvidenceMeans': 'UNKNOWN;not-not-executed-or-failed',
        'gameRunningInferred': False, 'steamHealthInferred': False, 'recoverySuccessInferred': False,
        'physicalDeliveryInferred': False, 'currentActionAuthorityInferred': False,
        'writerBirthBackfilled': False, 'pidMatchProvesCurrentLifetime': False,
        'readingRules': [
            'only exact flat event in Native lifecycle/detail streams;batch and recovery fields belong to different domains',
            'module-profile-ready reports module facts only;not game running,Steam health,recovery,permission or input delivery',
            'source identifies the reported trigger;not a writer,Steam process identity or causal proof',
            'missing,null,invalid and unfamiliar values stay distinct;prepared and exception fixture labels are not normalized',
            'all status counts include unrecognized scopes;recognizedRows additionally requires exact scope,known status and bounded nonempty source',
            'writer PID and birth use only this row;no manifest,subject PID,event ID or other-row backfill',
            'rotation and repeated ZIP occurrences remain independent;no deduplication,request pairs or unique attempts inferred',
            'bounded text prefixes and omitted observations retain original file references;not complete session history']}


PROCESS_EXPORT_CAPACITIES = {'virtual-adaptation-session.jsonl': 512, 'model-adaptation-session.jsonl': 128,
    'model-adaptation-signals-session.jsonl': 128}


def process_export_member_envelope(filename, rows, metadata, quality, data):
    stream = canonical_log_name(filename)
    maximum = PROCESS_EXPORT_CAPACITIES[stream]
    byte_count = len(data)
    invalid_count,non_object_count=_quality_error_counts(quality)
    issues, samples = collections.Counter(), []
    def problem(code, field=None, line=None):
        issues[code] += 1
        if len(samples) < 16:
            samples.append({'code': code, 'field': field, 'line': line})
    begin = rows[0][1] if rows and rows[0][1].get('record') == 'process_begin' else {}
    end = rows[-1][1] if rows and rows[-1][1].get('record') == 'process_end' else {}
    result = {'file': filename, **metadata, 'bytes': byte_count, 'status': 'unverified-envelope',
        'completeRetainedSnapshotClaimSupported': False, 'retainedRowsConsistencyVerified': False,
        'maxSupportedCapacity': maximum, 'validObjectRowsObserved': len(rows),
        'quality': {'lineCount': quality['lineCount'], 'invalidRowCount': invalid_count,
            'nonObjectLineCount': non_object_count, 'endsWithNewline': quality['endsWithNewline'],
            'replacementCharacterCount': quality['utf8ReplacementCharacters']},
        'begin': {'reported': {}, 'fieldStates': {}}, 'end': {'reported': {}, 'fieldStates': {}},
        'sourceCollectionCompleteProven': False, 'durabilityProven': False, 'physicalActionProven': False,
        'copyCutProven': False, 'sourceDamageProven': False, 'writerIdentityOrCurrentLifetimeInferred': False}
    def field(source, location, key, kind, nullable=False):
        view = result[location]
        if key not in source:
            view['fieldStates'][key] = 'missing';problem('missing-field', location + '.' + key);return None
        value = source[key]
        if value is None:
            view['reported'][key] = None;view['fieldStates'][key] = 'null'
            if not nullable:problem('null-required-field', location + '.' + key)
            return None
        valid = (type(value) is bool if kind == 'bool' else
                 type(value) is int and 0 <= value < (1 << 64))
        view['fieldStates'][key] = 'recorded' if valid else 'invalid-type-or-range'
        if valid:
            view['reported'][key] = value;return value
        problem('invalid-field', location + '.' + key);return None
    def uint(value):
        return type(value) is int and 0 <= value < (1 << 64)
    if not begin:problem('missing-first-begin-marker')
    if not end:problem('missing-last-end-marker')
    if begin.get('schema') != 'ymcc.adaptation-process-export/1':problem('unsupported-or-missing-schema')
    if rows and rows[0][0] != 1:problem('begin-not-first-physical-line', line=rows[0][0])
    if rows and rows[-1][0] != quality['lineCount']:problem('end-not-last-physical-line', line=rows[-1][0])
    if quality['invalidRows']:problem('uninterpreted-byte-lines')
    if quality['nonObjectLines']:problem('non-object-lines')
    blanks=None if invalid_count is None or non_object_count is None else quality['lineCount']-len(rows)-invalid_count-non_object_count
    if blanks is None:problem('quality-counts-unavailable')
    result['quality']['blankLineCount'] = blanks
    if blanks:problem('blank-lines-in-envelope')
    if not quality['endsWithNewline']:problem('unterminated-final-line')
    if byte_count > 4 * 1024 * 1024:problem('native-file-byte-budget-exceeded')
    inspected = rows[:maximum + 2]
    if len(rows) > len(inspected):
        problem('object-row-inspection-budget-exceeded');inspected = inspected + [rows[-1]]
    result['objectRowsInspected'] = len(inspected)
    result['objectRowsNotInspected'] = len(rows) - len(inspected)
    line_bytes = {}
    if byte_count <= 4 * 1024 * 1024:
        cursor = 0
        for number in range(1, min(quality['lineCount'], maximum + 2) + 1):
            newline = data.find(b'\n', cursor)
            stop = byte_count if newline < 0 else newline + 1
            line_bytes[number] = stop - cursor;cursor = stop
        final_stop = byte_count - int(data.endswith(b'\n'))
        final_start = data.rfind(b'\n', 0, max(0, final_stop)) + 1
        if quality['lineCount']:line_bytes[quality['lineCount']] = byte_count - final_start
    result['objectRowByteSizesAvailable'] = all(line in line_bytes for line, _ in inspected)
    for line, row in inspected:
        size = line_bytes.get(line)
        if size is None:problem('object-row-byte-size-not-inspected', line=line)
        elif size > (65536 if row is end else 32768):problem('native-record-byte-budget-exceeded', line=line)
    if sum(row.get('record') == 'process_begin' for _, row in inspected) != 1:problem('begin-marker-count-not-one')
    if sum(row.get('record') == 'process_end' for _, row in inspected) != 1:problem('end-marker-count-not-one')
    available = field(begin, 'begin', 'snapshotAvailable', 'bool')
    end_available = field(end, 'end', 'snapshotAvailable', 'bool')
    capacity = field(begin, 'begin', 'capacity', 'uint')
    known = field(begin, 'begin', 'knownFactsAtSnapshot', 'uint', True)
    end_known = field(end, 'end', 'knownFactsAtSnapshot', 'uint', True)
    busy = field(begin, 'begin', 'busyDropsAtSnapshot', 'uint', True)
    drops = field(begin, 'begin', 'capacityDropsAtSnapshot', 'uint', True)
    written = field(end, 'end', 'writtenFacts', 'uint')
    omitted = field(end, 'end', 'omittedFacts', 'uint')
    complete = field(end, 'end', 'retainedSnapshotRowsComplete', 'bool')
    for location, source, key in (('begin', begin, 'durabilityInferred'), ('begin', begin, 'completeHistory'),
        ('end', end, 'sourceFirstWitnessCoverageComplete'), ('end', end, 'completeHistory')):
        if field(source, location, key, 'bool') is True:problem('unsupported-positive-coverage-claim', location + '.' + key)
    capacity_valid = uint(capacity) and 0 < capacity <= maximum
    if uint(capacity) and not capacity_valid:problem('capacity-outside-supported-range')
    known_valid = uint(known) and capacity_valid and known <= capacity
    if type(available) is bool and type(end_available) is bool and available != end_available:problem('snapshot-availability-disagrees')
    if available is True:
        if not known_valid:problem('available-snapshot-known-count-unusable')
        if not uint(end_known) or known != end_known:problem('snapshot-known-count-disagrees')
        if not uint(busy) or not uint(drops):problem('available-snapshot-drop-counters-unusable')
    elif available is False:
        if any(value is not None for value in (known, end_known, busy, drops)):problem('unavailable-snapshot-has-known-counts')
    witness_ordinals, omission_ordinals = set(), set()
    witness_count = 0
    virtual = stream == 'virtual-adaptation-session.jsonl'
    witness_phases = {} if virtual else None
    repeated_witness_ordinals = set() if virtual else None
    def ordinal(value, target, location, line):
        if type(value) is not int or not 1 <= value <= maximum:
            problem('invalid-slot-ordinal', location, line);return
        if known_valid and value > known:
            problem('slot-ordinal-exceeds-known', location, line);return
        if value in target:problem('duplicate-slot-ordinal', location, line)
        target.add(value)
    for line, row in inspected:
        if row is begin or row is end:continue
        if row.get('record') != 'first_witness':
            problem('unsupported-body-record', line=line);continue
        witness_count += 1
        slot = row.get('slotOrdinal')
        ordinal(slot, witness_ordinals, 'witness.slotOrdinal', line)
        if 'firstSample' not in row:problem('witness-first-sample-missing', line=line)
        if virtual and type(slot) is int and slot in witness_ordinals:
            if slot in witness_phases:repeated_witness_ordinals.add(slot)
            witness_phases[slot] = row.get('queuePhaseAtSnapshot')
    omission_rows = end.get('omissions')
    if not isinstance(omission_rows, list):
        result['end']['fieldStates']['omissions'] = 'missing' if 'omissions' not in end else 'null' if omission_rows is None else 'invalid-type'
        problem('omissions-list-unavailable');omission_rows = None
    else:
        result['end']['fieldStates']['omissions'] = 'recorded'
        result['omissionEntriesObserved'] = len(omission_rows)
        result['omissionEntriesNotInspected'] = max(0, len(omission_rows) - maximum)
        if len(omission_rows) > maximum:problem('omission-inspection-budget-exceeded')
        reasons = collections.Counter()
        for row in omission_rows[:maximum]:
            if not isinstance(row, dict):problem('non-object-omission');continue
            ordinal(row.get('slotOrdinal'), omission_ordinals, 'omissions.slotOrdinal', rows[-1][0] if rows else None)
            reason = row.get('reason')
            if reason in ('row-byte-budget', 'file-byte-budget', 'sample-serialization-failed'):
                reasons[reason] += 1
            else:
                reasons['unrecognized'] += 1;problem('unsupported-omission-reason')
        result['reportedOmissionReasons'] = dict(reasons)
        if uint(omitted) and omitted != len(omission_rows):problem('omitted-count-disagrees')
    if virtual:
        counts = dict.fromkeys(('append-reported', 'key-already-present', 'budget-omitted'), 0)
        resolved = {slot for slot, phase in witness_phases.items() if phase == 'drain-resolved'}
        resolved -= (repeated_witness_ordinals | omission_ordinals)
        reported = end.get('journalDrainOutcomes', {})
        claims, duplicated = {}, set()
        invalid = 0
        if not isinstance(reported, dict):
            invalid += 1
        else:
            invalid += len(reported) - sum(key in reported for key in counts)
            for key in counts.keys() & reported.keys():
                slots = reported[key]
                if not isinstance(slots, list):
                    invalid += 1
                    continue
                if len(slots) > maximum:
                    invalid += len(slots)
                    continue
                for slot in slots:
                    if type(slot) is not int or slot not in resolved:
                        invalid += 1
                    elif slot in claims:
                        invalid += 1
                        duplicated.add(slot)
                    else:
                        claims[slot] = key
        for slot in claims.keys() - duplicated:
            counts[claims[slot]] += 1
        result['journalDrainOutcomes'] = {'reportedCounts': counts,
            'missingOnResolvedRows': len(resolved) - sum(counts.values()), 'invalidOrUnexpectedRows': invalid,
            'scope': 'footer claims joined to inspected resolved witnesses in this member;missing means unknown;not durable bytes or complete history'}
    if uint(written) and written != witness_count:problem('written-count-disagrees')
    overlap = witness_ordinals & omission_ordinals
    if overlap:problem('witness-and-omission-overlap')
    missing = sorted(set(range(1, known + 1)) - witness_ordinals - omission_ordinals) if known_valid else []
    result.update(witnessRowsInspected=witness_count, uniqueWitnessOrdinalsInspected=len(witness_ordinals),
        uniqueOmissionOrdinalsInspected=len(omission_ordinals), missingSlotCount=len(missing) if known_valid else None,
        missingSlotOrdinalsFirst16=missing[:16], missingSlotOrdinalsNotListed=max(0, len(missing) - 16),
        overlappingSlotCount=len(overlap), overlappingSlotOrdinalsFirst16=sorted(overlap)[:16])
    if available is True and known_valid:
        if uint(written) and uint(omitted) and written + omitted != known:problem('known-not-written-plus-omitted')
        if missing:problem('known-slots-not-accounted-for')
    if available is False and (witness_count or omission_rows or written != 0 or omitted != 0):problem('unavailable-snapshot-has-payload')
    if type(complete) is bool and type(available) is bool and omission_rows is not None:
        if complete != (available and not omission_rows):problem('complete-flag-disagrees-with-snapshot')
    result['issueCounts'], result['issueSamples'] = dict(issues), samples
    result['issueObservationsNotListed'] = sum(issues.values()) - len(samples)
    if not issues:
        result['retainedRowsConsistencyVerified'] = True
        result['status'] = ('consistent-unavailable-snapshot' if available is False else
            'consistent-with-omissions' if omitted else 'consistent-complete-retained-snapshot')
        result['completeRetainedSnapshotClaimSupported'] = available is True and complete is True
    return result

class ProcessExportEnvelopeEvidence:
    """Inspect immediately;retain at most28 reports without keeping raw member bytes."""
    def __init__(self):
        self.prefix, self.tail, self.first_status = [], collections.deque(maxlen=12), {}
        self.counts, self.total = collections.Counter(), 0

    def observe(self, filename, rows, metadata, quality, data):
        if canonical_log_name(filename) not in PROCESS_EXPORT_CAPACITIES:return
        item = process_export_member_envelope(filename, rows, metadata, quality, data)
        indexed = self.total, item;self.total += 1;self.counts[item['status']] += 1
        if len(self.prefix) < 12:self.prefix.append(indexed)
        self.tail.append(indexed);self.first_status.setdefault(item['status'], indexed)
        return item

    def evidence(self):
        selected = {index: item for index, item in [*self.prefix, *self.tail, *self.first_status.values()]}
        return {'schemaVersion': 1, 'scope': 'retained-member-envelope-self-consistency;not-native-origin-or-whole-source-proof',
            'membersObserved': self.total, 'countsByStatus': dict(self.counts), 'maxListedMembers': 28,
            'membersNotListed': self.total - len(selected), 'members': [selected[key] for key in sorted(selected)],
            'selection': 'first12,last12,and-first-of-four-fixed-statuses;archive-encounter-order-only',
            'sourceCollectionCompleteProven': False, 'durabilityProven': False, 'physicalActionProven': False,
            'rotationOrDuplicateMembersMerged': False,
            'readingRules': [
                'existing currentProcessExports flags remain producer reports;this view checks physical lines,counts,byte budgets and exact slot accounting',
                'clean counts and ordinals support only the copied snapshot claim;not source completeness,live authority,physical success or native authenticity',
                'busy unavailable snapshot has null known/drop counts and zero payload;it never means no input occurred',
                'malformed/nonobject/blank/unterminated lines prevent a supported complete claim;copy truncation or source corruption is never inferred',
                'literal Unicode replacement characters are payload when strict decoding succeeds;actual invalid-row evidence alone rejects decoding',
                'at most capacity-bound rows and omissions are inspected;overflow is explicit and cannot grant completeness',
                'member bytes are inspected immediately and not retained by this collector;only28 bounded reports remain',
                'ordinals are snapshot slots,not time or operation identity;duplicates and rotations remain independent',
                'absence of a retained member is UNKNOWN;neither missing export nor event absence is inferred']}


def process_export_envelope_evidence(members):
    collector = ProcessExportEnvelopeEvidence()
    for filename, rows, metadata, quality, data in members:
        collector.observe(filename, rows, metadata, quality, data)
    return collector.evidence()


VIRTUAL_JOURNAL = 'virtual-adaptation.log'
VIRTUAL_SESSION = 'virtual-adaptation-session.jsonl'
VIRTUAL_JOURNAL_MAX_BYTES = 8 * 1024 * 1024
VIRTUAL_JOURNAL_MAX_ROWS = 4096
VIRTUAL_FRAME_SAMPLE_MARKER = 'virtualFrameSampleSchema'
VIRTUAL_FRAME_SAMPLE_DEFAULTS = {
    'transportSecretsRetained': False,
    'requestFields': 'allowlisted-controls-and-tuple;startupNonce-and-transport-auth-omitted',
    'bindingCapturedBeforeRequest': True,
    'frameScope': 'actual-submit-control-values;transport-auth-omitted;not-driver-report-bytes',
    'hostSdkDeliveryProven': False, 'physicalSuppressionProven': False,
    'steamConsumptionProven': False, 'gameConsumptionProven': False,
}


def virtual_journal_key(row):
    key = row.get('uniqueKey')
    if (not isinstance(key, dict) or key.keys() != {'schema', 'model', 'category', 'identity'} or
            type(key['schema']) is not int or key['schema'] != 1 or
            not isinstance(key['model'], dict) or not isinstance(key['identity'], dict) or
            not isinstance(key['category'], str) or not key['category'].startswith('virtual-') or
            row.get('category') != key['category']):
        return None
    return json.dumps(key, sort_keys=True, separators=(',', ':'), allow_nan=False)


def virtual_journal_first_sample(row):
    sample = row.get('firstSample')
    if not isinstance(sample, dict):
        return None
    if VIRTUAL_FRAME_SAMPLE_MARKER not in row:
        return sample
    marker = row[VIRTUAL_FRAME_SAMPLE_MARKER]
    key = row.get('uniqueKey')
    if (type(marker) is not int or marker != 1 or row.get('schema') != 'ymcc.model-adaptation/1' or
            row.get('event') != 'virtual-adaptation-witness' or
            row.get('category') not in ('virtual-submit-first-witness', 'virtual-sdk-first-witness') or
            not isinstance(key, dict) or key.get('category') != row['category'] or
            not VIRTUAL_FRAME_SAMPLE_DEFAULTS.keys().isdisjoint(sample)):
        return None
    return {**sample, **VIRTUAL_FRAME_SAMPLE_DEFAULTS}


def virtual_journal_comparison(member_counts, journal, session):
    result = {'scope': 'same-zip-exact-root-members;recorded-keys-and-samples-only', 'state': 'member-unavailable',
        'memberCounts': {name: member_counts[name] for name in (VIRTUAL_JOURNAL, VIRTUAL_SESSION)},
        'counts': None, 'firstExamples': {}, 'journalRows': None, 'sessionWitnessRows': None,
        'retainedJournalBudget': None,
        'writerDispositionOrDurabilityInferred': False, 'membersCapturedAtomicallyProven': False}
    if any(count > 1 for count in result['memberCounts'].values()):
        result['state'] = 'ambiguous-repeated-member';return result
    if journal is None or session is None:
        return result
    journal_rows, quality, size = journal
    session_rows, envelope = session
    result.update(journalRows=len(journal_rows), sessionWitnessRows=envelope['witnessRowsInspected'])
    if envelope['status'] not in ('consistent-complete-retained-snapshot', 'consistent-with-omissions'):
        result['state'] = 'session-envelope-unusable';return result
    if (size > VIRTUAL_JOURNAL_MAX_BYTES or len(journal_rows) > VIRTUAL_JOURNAL_MAX_ROWS or
            (size and not quality['endsWithNewline']) or _quality_error_counts(quality) != (0, 0) or
            quality['lineCount'] != len(journal_rows)):
        result['state'] = 'journal-coverage-insufficient';return result
    index = {}
    for line, row in journal_rows:
        if row.get('schema') != 'ymcc.model-adaptation/1' or row.get('event') != 'virtual-adaptation-witness':
            result['state'] = 'journal-row-unrecognized';return result
        key = virtual_journal_key(row)
        sample = virtual_journal_first_sample(row)
        if key is None or sample is None:
            result['state'] = 'journal-row-unrecognized';return result
        index[key] = None if key in index else (line, sample)
    counts = dict.fromkeys(('key-not-observed', 'ambiguous-journal-key', 'sample-equal', 'sample-different',
                           'unrecognized-session-witness'), 0)
    for line, row in session_rows[1:-1]:
        key = virtual_journal_key(row)
        sample = row.get('firstSample')
        example = {'sessionLine': line, 'slotOrdinal': row['slotOrdinal']}
        if key is None or not isinstance(sample, dict):
            state = 'unrecognized-session-witness'
        elif key not in index:
            state = 'key-not-observed'
        elif index[key] is None:
            state = 'ambiguous-journal-key'
        else:
            journal_line, old_sample = index[key]
            example['journalLine'] = journal_line
            state = 'sample-equal' if (json.dumps(sample, sort_keys=True, allow_nan=False) ==
                                       json.dumps(old_sample, sort_keys=True, allow_nan=False)) else 'sample-different'
        counts[state] += 1
        result['firstExamples'].setdefault(state, example)
    result.update(state='compared-retained-members', counts=counts, retainedJournalBudget={
        'scope': 'validated-archived-journal-only;not-live-writer-state-or-append-admission',
        'retainedBytes': size, 'maxBytes': VIRTUAL_JOURNAL_MAX_BYTES,
        'byteHeadroom': VIRTUAL_JOURNAL_MAX_BYTES - size,
        'retainedRows': len(journal_rows), 'maxRows': VIRTUAL_JOURNAL_MAX_ROWS,
        'rowHeadroom': VIRTUAL_JOURNAL_MAX_ROWS - len(journal_rows)})
    return result


class EvidenceReferenceSelection:
    """Select bounded references before expanding diagnostic projections."""
    max_rows = 128
    max_head_rows = 128
    max_descriptors = 64

    def __init__(self):
        self.total = self.untracked = self.issue_rows = 0
        self.head = []
        self.tail = collections.deque(maxlen=self.max_rows)
        self.types = {}

    @staticmethod
    def scalar_fields(prefix, values, fields):
        key, description = [prefix], {}
        for name in fields:
            if name not in values:
                shape = ('missing',)
            else:
                value = values[name]
                if value is None:
                    shape = ('null',)
                elif type(value) is bool:
                    shape = ('bool', value)
                elif type(value) is int and -(1 << 63) <= value < (1 << 64):
                    shape = ('int', value)
                elif type(value) is float and math.isfinite(value):
                    shape = ('float', value.hex())
                elif isinstance(value, str) and len(value) <= 128:
                    shape = ('string', value)
                else:
                    return None, None
            key.append((name, shape))
            description[name] = {'state': shape[0]}
            if len(shape) == 2:
                description[name]['value'] = shape[1]
        return tuple(key), description

    def retain(self, ref, payload, key, description):
        self.total += 1
        entry = (self.total, ref, payload)
        if len(self.head) < self.max_head_rows:
            self.head.append(entry)
        self.tail.append(entry)
        if key is None:
            self.issue_rows += 1
            self.untracked += 1
        elif key in self.types:
            item = self.types[key]
            item['rows'] += 1
            item['last'] = ref
        elif len(self.types) < self.max_descriptors:
            self.types[key] = {'index': len(self.types), 'description': description,
                               'rows': 1, 'first': entry, 'last': ref}
        else:
            self.untracked += 1

    def selected(self):
        selected = {item['first'][0]: item['first'] for item in self.types.values()}
        for first, last in zip(self.head, reversed(self.tail)):
            for entry in (first, last):
                if len(selected) < self.max_rows:
                    selected.setdefault(entry[0], entry)
        return sorted(selected.values(), key=lambda q: q[0])

    def metadata(self, listed):
        descriptors = [{'index': item['index'], **item['description'], 'matchingRows': item['rows'],
            'first': item['first'][1], 'last': item['last']} for item in self.types.values()]
        return {'schemaVersion': 1, 'rowsObserved': self.total, 'maxListedRows': self.max_rows,
            'rowsNotListed': self.total - listed, 'selection': 'first-supported-descriptor-witnesses-then-balanced-head-tail;encounter-order-only',
            'descriptorTypes': descriptors, 'maxRetainedDescriptorTypes': self.max_descriptors,
            'descriptorIssueRows': self.issue_rows, 'untrackedDescriptorRows': self.untracked,
            'descriptorFieldCoverageComplete': None if self.total == 0 else self.untracked == 0,
            'retainedReferenceSlots': len(self.head) + len(self.tail) + len(self.types), 'maxRetainedReferenceSlots': 320}


def steam_diagnostic_reported_outcome(parent):
    # A normalized steamFailure object is also emitted for selected/started
    # and successful receipts. Only the same parent's boolean ok reports one
    # outcome;error domain/code and phase never supply a missing result.
    if 'ok' not in parent:
        return 'outcome-not-reported'
    if parent['ok'] is None:
        return 'outcome-null'
    if type(parent['ok']) is bool:
        return 'reported-ok' if parent['ok'] else 'reported-not-ok'
    return 'outcome-invalid-type'


def steam_diagnostic_phase_category(parent):
    if 'actionPhase' not in parent:
        return 'phase-not-reported'
    value = parent['actionPhase']
    if value is None:
        return 'phase-null'
    if not isinstance(value, str):
        return 'phase-invalid-type'
    return value if value in ('selected', 'started', 'finished', 'rejected', 'observation') else 'phase-unrecognized'


COMMON_PROJECTION_LIMITS = {
    'maxStringCharacters': 512, 'maxValueBytes': 4096, 'maxPayloadBytes': 16384,
    'maxValueNodes': 128, 'maxValueDepth': 6, 'maxContainerEntries': 64,
    'accounting': 'compact ASCII JSON;original file/line/writer references and omission descriptors excluded',
    'overflow': 'omit whole field value;no partial scalar,array or synthetic null;original row retained',
}


def bounded_common_projection_record(row):
    """Bound selected payload values while preserving ordinary values and refs."""
    protected = {'file', 'line', 'archiveEntryOccurrence', 'containerOnly', 'writerPid',
        'writerIdentityField', 'currentPidMatch', 'writerProcessCreation100ns', 'writerLifetimeKnown'}
    sections = {'readiness', 'result', 'nativeQualification', 'receipt', 'contract', 'context', 'failure'}
    result, slots, omissions = dict(row), [], []
    for key, value in row.items():
        if key in protected:
            continue
        if key in sections and isinstance(value, dict):
            result[key] = dict(value)
            slots.extend((key + '.' + field, result[key], field, part) for field, part in value.items())
        else:
            slots.append((key, result, key, value))

    class OmitValue(Exception):
        pass

    def size(value, depth, visited):
        visited[0] += 1
        if visited[0] > 128:
            raise OmitValue('value-node-limit')
        if depth > 6:
            raise OmitValue('value-depth-limit')
        if isinstance(value, str):
            if len(value) > 512:
                raise OmitValue('string-character-limit')
            if any(0xD800 <= ord(c) <= 0xDFFF for c in value):
                raise OmitValue('unpaired-surrogate')
        elif type(value) is int:
            if value.bit_length() > 128:
                raise OmitValue('integer-size-limit')
        elif type(value) is float:
            if not math.isfinite(value):
                raise OmitValue('nonfinite-number')
        elif value is None or type(value) is bool:
            pass
        elif isinstance(value, (list, dict)):
            if len(value) > 64:
                raise OmitValue('container-entry-limit')
            total = 2 + max(0, len(value) - 1)
            for key, part in value.items() if isinstance(value, dict) else enumerate(value):
                if isinstance(value, dict):
                    if not isinstance(key, str) or len(key) > 128:
                        raise OmitValue('object-key-limit')
                    total += size(key, depth + 1, visited) + 1
                total += size(part, depth + 1, visited)
                if total > 4096:
                    raise OmitValue('value-byte-limit')
            return total
        else:
            raise OmitValue('unsupported-value-type')
        return len(json.dumps(value, ensure_ascii=True, allow_nan=False, separators=(',', ':')))

    # Reserve container/key punctuation; small status fields are considered
    # before bulky text,without reordering the returned dictionaries.
    spent = 256
    priority = lambda slot: 0 if slot[3] is None or type(slot[3]) in (bool, int, float) else 1 if isinstance(slot[3], str) and len(slot[3]) <= 128 else 2
    for path, target, key, value in sorted(slots, key=priority):
        try:
            amount = size(value, 0, [0])
            if amount > 4096:
                raise OmitValue('value-byte-limit')
            charge = amount + len(json.dumps(key, ensure_ascii=True)) + 2
            if spent + charge > 16384:
                raise OmitValue('selected-payload-byte-limit')
            spent += charge
        except OmitValue as error:
            target.pop(key)
            omissions.append({'field': path, 'reason': str(error)})
    if omissions:
        result['projectionOmissions'] = omissions
    return result



class SteamFailureEvidence(EvidenceReferenceSelection):
    """Bounded summary references; complete received bytes stay in their files."""
    # Five finite outcomes have independent witnesses even when the descriptor
    # table fills. Reuse five head slots: 123 + 128 tail + 64 types + 5 = 320.
    max_outcome_witnesses = 5
    max_head_rows = EvidenceReferenceSelection.max_rows - max_outcome_witnesses

    def __init__(self):
        super().__init__()
        self.outcomes = collections.Counter()
        self.phases = collections.Counter()
        self.outcome_first = {}

    @staticmethod
    def descriptor(location, failure):
        fields = ('schema', 'domain', 'sourceStage', 'codeKnown', 'code',
                  'exceptionClassKnown', 'exceptionClass')
        key, description = EvidenceReferenceSelection.scalar_fields(location, failure, fields)
        if key is None:
            return None, None
        return key, {'location': location + '.steamFailure', 'fields': description}

    def observe(self, ref, location, parent, failure):
        key, description = self.descriptor(location, failure)
        outcome = steam_diagnostic_reported_outcome(parent)
        phase = steam_diagnostic_phase_category(parent)
        self.outcomes[outcome] += 1
        self.phases[phase] += 1
        if key is not None:
            key += (('reportedOutcome', outcome), ('phaseCategory', phase))
            description.update(reportedOutcome=outcome, phaseCategory=phase)
        payload = (location, parent, failure)
        self.retain(ref, payload, key, description)
        if outcome not in self.outcome_first:
            self.outcome_first[outcome] = (self.total, ref, payload)

    def selected(self):
        selected = {entry[0]: entry for entry in self.outcome_first.values()}
        for item in self.types.values():
            selected.setdefault(item['first'][0], item['first'])
        for first, last in zip(self.head, reversed(self.tail)):
            for entry in (first, last):
                if len(selected) < self.max_rows:
                    selected.setdefault(entry[0], entry)
        return sorted(selected.values(), key=lambda entry: entry[0])

    def metadata(self, listed):
        result = super().metadata(listed)
        result.update(
            selection='first-finite-outcomes-then-supported-descriptors-then-balanced-head-tail;encounter-order-only',
            retainedReferenceSlots=result['retainedReferenceSlots'] + len(self.outcome_first),
            maxHeadReferenceSlots=self.max_head_rows, maxOutcomeWitnesses=self.max_outcome_witnesses,
            outcomeFirstWitnesses=[{'reportedOutcome': outcome, 'first': entry[1]}
                for outcome, entry in self.outcome_first.items()],
            outcomeWitnessScope='first received field per finite reported outcome across inspected members;not per writer,operation or complete transition history')
        return result

    def finish(self, project):
        rows = []
        for _, ref, payload in self.selected():
            location, parent, failure = payload
            rows.append(bounded_common_projection_record({**ref, 'location': location + '.steamFailure',
                'reportedOutcome': steam_diagnostic_reported_outcome(parent),
                'context': project(parent, ('actionId', 'recoveryEpisode', 'generation', 'nativePowerGeneration',
                    'steamPid', 'steamBirth', 'actionPhase', 'actionOperation', 'ok', 'mutated', 'uncertain', 'requestDispatched')),
                'failure': project(failure, ('schema', 'domain', 'sourceStage', 'codeKnown', 'code', 'exceptionClassKnown',
                    'exceptionClass', 'lineKnown', 'line', 'columnKnown', 'column', 'signatures', 'textInspectedBytes', 'textTruncated')),
                'interpretation': 'producer-reported-normalized-failure;absent-code-and-domain-remain-unknown'}))
        metadata = self.metadata(len(rows))
        metadata.update({
            'countUnit': 'received named steamFailure fields;not unique failures,operations,attempts or processes',
            'descriptorScope': 'location and typed schema/domain/sourceStage/codeKnown/code/exceptionClassKnown/exceptionClass plus finite same-parent outcome/phase categories;not every error attribute',
            'reportedOutcomeCounts': dict(self.outcomes), 'reportedPhaseCounts': dict(self.phases),
            'normalizedDiagnosticFieldProvesOperationFailure': False,
            'reportedCountsAreUniqueOperations': False, 'missingOutcomeInferredFromPhaseOrCode': False,
            'originalProjectedRowsUnchanged': not any('projectionOmissions' in row for row in rows), 'rawFileBytesRemoved': False,
            'projectionFieldBudget': {**COMMON_PROJECTION_LIMITS, 'rowsWithOmittedFields': sum('projectionOmissions' in row for row in rows)},
            'descriptorCountsAcrossMembersAreActionJoins': False, 'completeHistoryProven': False,
            'readingRules': ['all rows stay available in the original retained files;summary omission is not source loss',
                'missing/null/bool/int/float/string stay distinct;unsupported or overlong descriptor values do not grow the map',
                'untrackedDescriptorRows counts received fields,not unique missing error types',
                'selected field values outside explicit payload budgets are omitted with original row references;no partial values or synthetic nulls',
                'head/tail use archive encounter order;no clock sort,current-writer selection or lifetime inference']})
        return rows, metadata


class CommonObservationEvidence(EvidenceReferenceSelection):
    """Received field shapes only; never synthesize an operation timeline."""
    paths = {
        'readiness': ((0, ('event',)), (1, ('phase', 'reason')),
            (2, ('operationAllowed', 'basis', 'sharedPowerLifecycle', 'sharedReady', 'finalKernelWakeAdmitted',
                 'rejectionReason', 'rejectedGuard', 'gameInputVerified', 'continueButtonVerified'))),
        'engine': ((0, ('event', 'engineEvent', 'recoveryRoute')),
            (2, ('status', 'ok', 'reason', 'route', 'recoveryRoute', 'driverEntered', 'publisherCallsKnown',
                 'consumed', 'nativeRecovered', 'inputVerified', 'mutationCommitAttempted',
                 'repairAccepted', 'uiVerified', 'referenceCaptured'))),
        'fingerprint': ((0, ('event',)), (1, ('ok', 'accepted', 'reason', 'mutated', 'scope', 'stage')),
            (2, ('profile', 'sourceLength', 'sourceFnv1a32'))),
    }

    def __init__(self, kind):
        super().__init__()
        self.kind = kind
        self.engine_tags = collections.Counter()
        prefixes = ('row', 'details', 'value')
        self.descriptor_paths = tuple((prefixes[index] + '.' + name, index, name)
            for index, names in self.paths[kind] for name in names)
        self.descriptor_names = tuple(path[0] for path in self.descriptor_paths)

    def observe(self, ref, row, detail, value):
        # Separate containers in descriptor names; an absent field in one
        # container must never borrow a similarly named field from another.
        containers = (row, detail, value)
        descriptor_values = {fullname: containers[index][name]
            for fullname, index, name in self.descriptor_paths if name in containers[index]}
        descriptor_names = self.descriptor_names
        if self.kind == 'readiness':
            proof_fields = consumer_proof_selection_fields(value)
            descriptor_values.update(('proof.' + name, field) for name, field in proof_fields.items())
            descriptor_names += tuple('proof.' + name for name in proof_fields)
        key, fields = self.scalar_fields(self.kind, descriptor_values, descriptor_names)
        if self.kind == 'engine':
            tag = row.get('engineEvent') if row.get('event') == 'engine' else None
            self.engine_tags[tag if isinstance(tag, str) and tag in
                ('job-finished', 'result', 'observed', 'late-running-loss-observed') else 'other-or-unrecognized'] += 1
        self.retain(ref, (row, detail, value), key,
                    None if fields is None else {'kind': self.kind, 'fields': fields})

    def finish(self, render):
        rows = [bounded_common_projection_record(render(ref, *payload)) for _, ref, payload in self.selected()]
        metadata = self.metadata(len(rows))
        metadata.update(countUnit='received qualifying rows;not unique operations,attempts or processes',
            descriptorScope='fixed typed row/details/value fields for ' + self.kind + (' plus recorded proof evaluation/clock-limit categories' if self.kind == 'readiness' else '') + ';not complete semantic/type coverage',
            originalProjectedFieldsPreserved=not any('projectionOmissions' in row for row in rows), rawFileBytesRemoved=False, projectedOnlyAfterSelection=True,
            projectionFieldBudget={**COMMON_PROJECTION_LIMITS, 'rowsWithOmittedFields':sum('projectionOmissions' in row for row in rows)},
            completeHistoryProven=False, transitionsInferred=False, resourceReturnOrPhysicalInputProven=False,
            readingRules=['all unlisted rows remain in their original retained files',
                'first descriptor witness is not every state change or a complete per-request lifecycle',
                'missing/null/bool/int/float/string are separate;descriptor floats use float.hex,within-budget projected values stay unchanged',
                'over-budget fields carry projectionOmissions with original file/line;no partial values or synthetic nulls',
                'descriptor overflow counts received rows,not unique missing types',
                'encounter order and raw references are preserved;no writer-birth backfill,clock sort or operation join'])
        if self.kind == 'engine':
            metadata['reportedEngineEventTags'] = dict(self.engine_tags)
            metadata['jobFinishedTagProvesResourceReturn'] = False
            metadata['reportedStatusProvesPhysicalInput'] = False
        return rows, metadata




NATIVE_CONTROL_FIELDS=('rawLtByte','rawRtByte','lt','rt','ltRaw','rtRaw','lxShort','lyShort','rxShort','ryShort',
    'leftTouchX','leftTouchY','rightTouchX','rightTouchY','leftPressure','rightPressure','gyroX','gyroY','gyroZ','accelX','accelY','accelZ','qw','qx','qy','qz')
NATIVE_CONTROL_SCOPE='all calls to this native final-frame observer since prior witness;includes non-submitted frames;not every physical packet or SDK/internal report;touch bits present=1,left-down=2,right-down=4,left-click=8,right-click=16;normal IMU ranges are not anomaly classification'
NATIVE_CONTROL_SOURCE_SCOPE='selected native source snapshot before mapping;not a fresh hardware packet read or device/UU ownership proof'
NATIVE_SIGNAL_COUNTER_SCOPE='same-native-thread-since-last-witness;may-span-targets;not-total-bus-callbacks'


def _control_uint(value,bits=64):
    return type(value) is int and 0<=value<(1<<bits)


def _control_finite(value):
    if type(value) not in (int,float):return False
    try:return math.isfinite(value)
    except (OverflowError,ValueError):return False


def _native_control_window_record(value,wrapper=True):
    result={'state':'null' if value is None else 'invalid-type','reported':{},'ranges':{'state':'not-inspected','fields':[]},
        'selectedSource':{'state':'not-inspected'},'controlEdges':{'state':'not-inspected','observedRows':None,'observations':[]},'fieldIssues':[],'fieldIssueCount':0}
    if not isinstance(value,dict):return result
    if wrapper and value.get('snapshotKnown') is False:
        result['state']='reported-contended' if value.get('reason')=='contended' else 'reported-unavailable';return result
    if type(value.get('schemaVersion')) is not int or value['schemaVersion']!=1:result['state']='unsupported-schema';return result
    if value.get('scope')!=NATIVE_CONTROL_SCOPE:result['state']='unsupported-scope';return result
    fields=result['reported']
    def issue(key):
        result['fieldIssueCount']+=1
        if len(result['fieldIssues'])<32:result['fieldIssues'].append(key)
    def number(key,bits=64):
        v=value.get(key)
        if _control_uint(v,bits):fields[key]=v
        else:issue(key+':missing-or-invalid')
    counters=('observations','submitAttempts','hostAcks','firstWireTick','lastWireTick','firstObservedTick','lastObservedTick',
        'firstSampleSequence','lastSampleSequence','bothRawTriggersNonzeroFrames','bothRawTriggers127Frames','nonfiniteValues',
        'controlEdgeTransitions','overwrittenOldestControlEdges','epoch','powerGeneration','discardedObservationsOnBindingChange')
    for key in counters:number(key)
    for key in ('buttonsOr','buttonsAnd','backButtonsOr','screenButtonsOr','touchStateOr'):number(key,32)
    for key in ('bindingKnown','sdkOrGameConsumptionProven')+(('snapshotKnown','drained','pendingTailAtExport') if wrapper else ()):
        v=value.get(key)
        if type(v) is bool:fields[key]=v
        else:issue(key+':missing-or-invalid')
    for key in ('runId','persona'):
        v=value.get(key)
        if isinstance(v,str) and len(v)<=128 and not any(0xd800<=ord(c)<=0xdfff for c in v) and len(v.encode('utf8'))<=128:fields[key]=v
        else:issue(key+':missing-or-invalid')
    if fields.get('sdkOrGameConsumptionProven') is True:issue('sdkOrGameConsumptionProven:unexpected-claim')
    if wrapper:
        for key in ('diagnosticContentionSinceProcessStart','overwrittenRetiredBindingWindows','lostRetiredBindingObservations'):number(key)
        if 'drained' in fields and 'pendingTailAtExport' in fields and fields['drained']==fields['pendingTailAtExport']:issue('pendingTailAtExport:contradictory-drain-flag')
        if 'drainPendingLogOfferReturn' in value or 'drainScope' in value:
            if value.get('drainPendingLogOfferReturn') is True:fields['drainPendingLogOfferReturn']=True
            else:issue('drainPendingLogOfferReturn:missing-or-invalid')
            if value.get('drainScope')!='prepared-copy;clear-only-after-original-log-offer-returns-and-window-unchanged;not-file-delivery-proof':issue('drainScope:unsupported')
            if fields.get('drained') is not False or fields.get('pendingTailAtExport') is not True:issue('drainPendingLogOfferReturn:contradictory-drain-flags')
    for left,right in (('submitAttempts','observations'),('hostAcks','submitAttempts'),('bothRawTriggersNonzeroFrames','observations'),('bothRawTriggers127Frames','bothRawTriggersNonzeroFrames'),('controlEdgeTransitions','observations')):
        if left in fields and right in fields and fields[left]>fields[right]:issue(left+':exceeds-'+right)
    arrays=[value.get(k) for k in ('minimum','maximum','fieldValueKnown')]
    order=value.get('fieldOrder');ranges=result['ranges']
    if not isinstance(order,list) or order!=list(NATIVE_CONTROL_FIELDS):ranges['state']='unsupported-field-order';issue('fieldOrder:unsupported')
    elif not all(isinstance(v,list) and len(v)==26 for v in arrays):ranges['state']='invalid-array-shape';issue('ranges:missing-or-invalid-array-shape')
    else:
        minimum,maximum,known=arrays;bad=False
        for i,name in enumerate(NATIVE_CONTROL_FIELDS):
            field={'field':name,'state':'unknown-value'}
            if type(known[i]) is not bool:field['state']='invalid-known-flag';issue(name+':invalid-known-flag');bad=True
            elif known[i] is True:
                if _control_finite(minimum[i]) and _control_finite(maximum[i]) and minimum[i]<=maximum[i]:field.update(state='reported-range',minimum=minimum[i],maximum=maximum[i])
                else:field['state']='invalid-reported-range';issue(name+':invalid-reported-range');bad=True
            ranges['fields'].append(field)
        ranges['state']='invalid-fields' if bad else 'reported-fields'
    source=value.get('selectedSource');selected={'state':'missing' if 'selectedSource' not in value else 'null' if source is None else 'invalid-type','reported':{},'triggerRanges':{'state':'not-inspected'}}
    if isinstance(source,dict):
        if source.get('scope')!=NATIVE_CONTROL_SOURCE_SCOPE:selected['state']='unsupported-scope';issue('selectedSource:unsupported-scope')
        else:
            before=result['fieldIssueCount'];reported=selected['reported']
            for key in ('observations','liveObservations','bothRawTriggers127Frames','buttonsOr'):
                v=source.get(key)
                if _control_uint(v,32 if key=='buttonsOr' else 64):reported[key]=v
                else:issue('selectedSource.'+key+':missing-or-invalid')
            for key in ('liveObservations','bothRawTriggers127Frames'):
                if key in reported and 'observations' in reported and reported[key]>reported['observations']:issue('selectedSource.'+key+':exceeds-observations')
            lo,hi=source.get('triggerMin'),source.get('triggerMax')
            if not isinstance(lo,list) or not isinstance(hi,list) or len(lo)!=2 or len(hi)!=2:issue('selectedSource.triggerRanges:invalid-array-shape')
            elif reported.get('observations')==0:selected['triggerRanges']={'state':'not-observed;stored-defaults-ignored'}
            elif 'observations' in reported:
                if all(_control_uint(a,8) and _control_uint(b,8) and a<=b for a,b in zip(lo,hi)):selected['triggerRanges']={'state':'reported-range','minimum':list(lo),'maximum':list(hi)}
                else:issue('selectedSource.triggerRanges:invalid-range')
            selected['state']='invalid-fields' if result['fieldIssueCount']!=before else 'reported-source-window'
    else:issue('selectedSource:missing-or-invalid')
    result['selectedSource']=selected
    edges=value.get('controlEdges');edge_view=result['controlEdges']
    if not isinstance(edges,list):edge_view['state']='missing-or-invalid-array';issue('controlEdges:missing-or-invalid-array')
    else:
        edge_view.update(state='reported-edges',observedRows=len(edges),maxListedRows=16,rowsNotInspected=max(0,len(edges)-16))
        if len(edges)>16:edge_view['state']='over-capacity';issue('controlEdges:exceeds16')
        indices=list(range(len(edges))) if len(edges)<=16 else list(range(8))+list(range(len(edges)-8,len(edges)))
        for index in indices:
            edge=edges[index];out={'index':index,'reported':{},'state':'invalid-type'}
            if isinstance(edge,dict):
                before=result['fieldIssueCount'];out['state']='reported-edge'
                for key in ('tick','observedTick','sampleSequence','sourceReadSequence','buttons','backButtons','screenButtons','touchBits','rawLtByte','rawRtByte','sourceButtons','sourceLtByte','sourceRtByte','wireTickKnown','sourceKnown','sourceLive'):
                    v=edge.get(key)
                    valid=(type(v) is bool if key in ('wireTickKnown','sourceKnown','sourceLive') else
                        type(v) is int and -(1<<31)<=v<(1<<31) if key in ('buttons','backButtons') else
                        _control_uint(v,8 if key in ('rawLtByte','rawRtByte','sourceLtByte','sourceRtByte') else 32 if key in ('screenButtons','touchBits','sourceButtons') else 64))
                    if valid:out['reported'][key]=v
                    else:issue('controlEdges['+str(index)+'].'+key+':missing-or-invalid')
                if result['fieldIssueCount']!=before:out['state']='invalid-fields'
            else:issue('controlEdges['+str(index)+']:invalid-type')
            edge_view['observations'].append(out)
        if all(k in fields for k in ('controlEdgeTransitions','overwrittenOldestControlEdges')) and fields['controlEdgeTransitions']!=len(edges)+fields['overwrittenOldestControlEdges']:issue('controlEdges:count-does-not-match-transitions-and-overwrite')
    result['state']='invalid-fields' if result['fieldIssueCount'] else 'reported-window'
    return result


CONTROLLER_LOCAL_VISIBILITY_EVENTS=('input-host-switch-audit','system-xinput-readback','xinput-slot-devices','input-host-open-result')


def connection_truth_log_record(row):
    result={'state':'legacy-connection-truth;reoffer-unconfirmed','reported':{},'fieldIssues':[],
        'newInputEdgeInferred':False,'deviceRecoveryProven':False,'uniqueControllerCountProven':False,'fileDeliveryProven':False}
    if not isinstance(row,dict) or row.get('event')!='domain-pad-truth':result['state']='unsupported-event';return result
    fields=result['reported'];issues=result['fieldIssues']
    for key in ('guide','connected','multipleActive','ymccForeground','hostPrepared'):
        value=row.get(key)
        if type(value) is bool:fields[key]=value
        else:issues.append(key+':missing-or-invalid')
    for key,maximum in [('connectedMask',15),('virtualEndpointMask',15),('admittedMask',15),('primarySlot',3),('leftTrigger',255),('rightTrigger',255)]:
        value=row.get(key)
        if type(value) is int and 0<=value<=maximum:fields[key]=value
        else:issues.append(key+':missing-or-invalid')
    if 'diagnosticRecoveryReoffer' not in row:
        if issues:result['state']='legacy-invalid-connection-truth'
        return result
    reoffer=row.get('diagnosticRecoveryReoffer')
    if type(reoffer) is not bool:issues.append('diagnosticRecoveryReoffer:invalid-type')
    else:fields['diagnosticRecoveryReoffer']=reoffer
    if row.get('observationScope')!='current-connection-truth;not-input-edge-or-device-recovery':issues.append('observationScope:unsupported')
    result['state']='invalid-connection-truth-fields' if issues else 'reported-current-truth-after-log-recovery' if reoffer else 'reported-original-connection-truth-observation'
    return result


def connection_truth_log_evidence(members,current_pid):
    head=[];tail=collections.deque(maxlen=8);witnesses={};counts=collections.Counter();total=0
    current=str(current_pid) if type(current_pid) is int and 0<current_pid<(1<<32) else current_pid if isinstance(current_pid,str) and 0<len(current_pid)<=10 and current_pid.isascii() and current_pid.isdecimal() and 0<int(current_pid)<(1<<32) else None
    for filename,rows,metadata in members:
        if not re.fullmatch(r'native-lifecycle\.log(?:\.(?:prev|previous|[0-9]+))?',filename):continue
        for line,row in rows:
            if row.get('event')!='domain-pad-truth':continue
            total+=1;record=connection_truth_log_record(row);counts[record['state']]+=1;pid=row.get('pid');birth=row.get('writerProcessCreation100ns');knownPid=type(pid) is int and 0<pid<(1<<32);knownBirth=isinstance(birth,str) and 0<len(birth)<=20 and birth.isascii() and birth.isdecimal() and 0<int(birth)<(1<<64)
            relation='unknown-writer-tuple' if not(knownPid and knownBirth) else 'current-pid-match' if current is not None and str(pid)==current else 'other-or-uncompared'
            item={'encounterOrdinal':total,'file':filename[:160],'line':line if type(line) is int and 0<line<(1<<64) else None,'writerPid':pid if knownPid else None,'writerProcessCreation100ns':birth if knownBirth else None,'writerIdentityProven':False,'pidRelation':relation,'record':record}
            if len(head)<8:head.append(item)
            else:tail.append(item)
            if record['state'] in ('reported-current-truth-after-log-recovery','invalid-connection-truth-fields','legacy-invalid-connection-truth'):witnesses.setdefault((relation,record['state']),item)
    selected={item['encounterOrdinal']:item for item in head+list(tail)+list(witnesses.values())}
    return {'schemaVersion':1,'rowsObserved':total,'rowsByState':dict(counts),'observations':[selected[k] for k in sorted(selected)],'maxListedObservations':25,'rowsNotListed':total-len(selected),
        'scope':'retained-domain-pad-truth-rows;current-observation-after-log-recovery-is-not-a-replayed-input-edge',
        'newInputEdgesCounted':False,'deviceRecoveryProven':False,'uniqueControllerCountProven':False,'fileDeliveryProven':False,'completeHistoryProven':False}


def slot_hid_enumeration_record(row):
    view={'state':'not-reported','scope':'same-row-Raw-Input-sample;not-device-absence-or-external-visibility'}
    if 'hidEnumeration' not in row:
        return view
    value=row['hidEnumeration']
    view['state']='invalid-fields'
    if not isinstance(value,dict) or type(value.get('schema')) is not int or value['schema']!=1:
        return view
    def uint(v):return type(v) is int and 0<=v<1<<32
    state=value.get('state');stage=value.get('firstFailureStage');error=value.get('firstWin32Error')
    size,count,scanned,failures=(value.get(k) for k in ('sizeCount','listCount','scannedCount','deviceReadFailures'))
    paths=row.get('hidGamepads')
    if (not all(k in value for k in ('sizeCount','listCount','firstFailureStage','firstWin32Error')) or
            type(value.get('pathLimit')) is not int or value['pathLimit']!=12 or
            not isinstance(paths,list) or len(paths)>12 or not uint(scanned) or not uint(failures)):
        return view
    if state=='size-query-failed':
        ok=size is None and count is None and scanned==failures==len(paths)==0 and stage=='list-size' and uint(error)
    elif state=='list-read-failed':
        ok=uint(size) and size>0 and count is None and scanned==failures==len(paths)==0 and stage=='list-read' and uint(error)
    elif state=='invalid-result':
        ok=(scanned==failures==len(paths)==0 and error is None and
            (stage=='list-size' and size is None and count is None or stage=='list-count' and uint(size) and size>0 and uint(count) and count>size))
    elif state in ('complete','partial-device-read','path-limit'):
        listed=0 if type(size) is int and size==0 and count is None else count
        ok=uint(size) and (size==0 and count is None or size>0 and uint(count)) and uint(listed) and len(paths)+failures<=scanned<=listed<=size
        ok=ok and (state=='complete' and scanned==listed and not failures or
                   state=='partial-device-read' and scanned==listed and failures>0 or state=='path-limit' and scanned<listed and len(paths)==12)
        ok=ok and (not failures and stage is None and error is None or failures>0 and
                   (stage in ('device-info','name-size','name-read') and uint(error) or
                    stage in ('name-size-invalid','name-data-invalid') and error is None))
    else:return view
    if ok:
        view.update(state=state,sizeCount=size,listCount=count,scannedCount=scanned,deviceReadFailures=failures,
                    firstFailureStage=stage,firstWin32Error=error,reportedPaths=len(paths))
    return view


def controller_local_visibility_record(row):
    result={'state':'unsupported-event','kind':None,'reported':{},'fieldIssues':[],
        'externalConsumerVisibilityProven':False,'uniqueControllerCountProven':False,'deviceOwnershipProven':False}
    if not isinstance(row,dict):return result
    event=row.get('event');issues=result['fieldIssues'];fields=result['reported']
    def flag(key):
        value=row.get(key)
        if type(value) is bool:fields[key]=value;return value
        issues.append(key+':missing-or-invalid');return None
    def mask(key):
        value=row.get(key)
        if type(value) is int and 0<=value<=15:fields[key]=value;return value
        issues.append(key+':missing-or-invalid');return None
    if event=='input-host-switch-audit' and row.get('beat')=='virtual-ready':
        result['kind']='local-target-ready'
        if row.get('phase')!='open' or row.get('policy')!='single-row-three-condition-assertion':result['state']='unsupported-local-ready-contract';return result
        for key in ('pad','consumerVisible','hideSkipped','physicalHidden','physicalSourcePending','enabled','mounted'):flag(key)
        if fields.get('consumerVisible') is True:issues.append('consumerVisible:unsupported-proof-claim')
        if fields.get('hideSkipped') is True and fields.get('physicalHidden') is True:issues.append('physicalHidden:contradicts-hide-skipped')
        result['state']='invalid-local-ready-fields' if issues else 'reported-local-target-ready'
        result['interpretation']='unconfirmed-local-relation'
        if not issues:
            if fields['physicalSourcePending'] and fields['hideSkipped'] and not fields['physicalHidden']:result['interpretation']='source-pending-with-hide-skipped'
            elif fields['physicalHidden'] and not fields['hideSkipped']:result['interpretation']='local-physical-hidden-reported'
            else:result['interpretation']='other-local-ready-boolean-relation'
        result['consumerVisibleFalseMeans']='this-row-does-not-prove-external-visibility;not-a-negative-consumer-enumeration'
        return result
    if event=='input-host-open-result':
        result['kind']='local-start-hide-admission'
        for key in ('hello','prepared','neutralized','visibilityAdmitted','physicalSourceAdmittedAtStart','physicalSourcePending','physicalHidAdmitted','transportFault'):flag(key)
        for key,allowed in [('physicalHideGate',('admitted','refused','not-required')),('physicalVisibility',('kept-visible-test','hidhide-admitted')),('pXinput',('UNENCLOSED','UNENCLOSED-source-pending'))]:
            part=row.get(key)
            if part in allowed:fields[key]=part
            else:issues.append(key+':missing-or-unsupported')
        if 'physicalVisibilitySchemaVersion' not in row:
            result['state']='legacy-local-open-admission;visibility-outcome-unconfirmed' if not issues else 'legacy-invalid-local-open-fields'
            result['legacyVisibilityLabelIsReadbackProof']=False
            if not issues:result['interpretation']='source-pending-at-log' if fields['physicalSourcePending'] else 'local-start-hide-admission-only'
            return result
        if type(row.get('physicalVisibilitySchemaVersion')) is not int or row['physicalVisibilitySchemaVersion']!=1 or row.get('physicalVisibilityScope')!='start-local-test-policy-and-hide-admission;not-physical-or-external-consumer-readback':result['state']='unsupported-open-visibility-contract';return result
        for key in ('physicalSourceOnlineAtHideGate','nativeHandoffRequiredAtHideGate','physicalVisibilityReadbackProven','externalControllerCountProven'):flag(key)
        for key in ('physicalVisibilityReadbackProven','externalControllerCountProven'):
            if fields.get(key) is not False:issues.append(key+':unsupported-proof-claim')
        result['state']='invalid-open-visibility-fields' if issues else 'reported-local-open-admission-only'
        result['legacyVisibilityLabelIsReadbackProof']=False
        result['interpretation']='source-pending-at-log' if fields.get('physicalSourcePending') is True else 'local-start-hide-admission-only'
        return result
    if event=='system-xinput-readback':
        result['kind']='same-process-xinput-readback'
        if row.get('source')!='system32-xinput1_4' or row.get('observerScope')!='same-YMCC-process; HidHide exemption may apply; not external Steam proof':result['state']='unsupported-xinput-observer-contract';return result
        if row.get('available') is False:
            result['state']='reported-local-xinput-api-unavailable' if row.get('reason')=='module-or-entrypoint-unavailable' else 'unsupported-xinput-unavailable-reason'
            fields['available']=False;return result
        reason=row.get('reason')
        if reason not in ('first','mask-change','buttons-change'):issues.append('reason:missing-or-unsupported')
        else:fields['reason']=reason
        value=mask('connectedMask')
        for key in ('virtualTargetEnabled','hostPrepared','ymccForeground'):flag(key)
        if value is not None:result.update(reportedConnectedXinputSlotCount=value.bit_count(),reportedConnectedXinputSlots=[i for i in range(4) if value&(1<<i)])
        result['state']='invalid-local-xinput-fields' if issues else 'reported-local-xinput-slots'
        result['slotCountScope']='reported-four-XInput-slot-bitmask-in-this-process;not-HID-or-Steam-controller-enumeration'
        return result
    if event=='xinput-slot-devices':
        result['kind']='native-xinput-slot-report';value=mask('connectedMask');mask('virtualEndpointMask')
        if 'admittedMask' in row or 'prevAdmittedMask' in row:
            mask('admittedMask');mask('prevAdmittedMask')
        result['hidEnumeration']=slot_hid_enumeration_record(row)
        for key in ('pendingDelete','physicalStable'):flag(key)
        slots=row.get('slots');seen=set();successMask=0;valid=True
        if not isinstance(slots,list) or len(slots)!=4:issues.append('slots:expected-four-reports');valid=False
        if isinstance(slots,list):
            for item in slots[:4]:
                if not isinstance(item,dict):valid=False;continue
                slot,code=item.get('slot'),item.get('result')
                if type(slot) is not int or not 0<=slot<4 or slot in seen or type(code) is not int or not 0<=code<(1<<32):valid=False;continue
                seen.add(slot)
                if code==0:successMask|=1<<slot
        if len(seen)!=4:valid=False
        if not valid:issues.append('slots:missing-invalid-or-duplicate-entry')
        elif value is not None and successMask!=value:issues.append('slots:success-results-disagree-with-connected-mask')
        if value is not None:result.update(reportedConnectedXinputSlotCount=value.bit_count(),reportedConnectedXinputSlots=[i for i in range(4) if value&(1<<i)])
        result['state']='invalid-native-slot-fields' if issues else 'reported-native-xinput-slot-results'
        result.update(hidPathOrHidControllerCountInspected=False,slotCountScope='this-Native-row-four-XInput-slot-reports;not-all-HID-or-external-controller-enumeration')
    return result


def controller_local_visibility_evidence(members,current_pid):
    banks={};total=0;counts=collections.Counter()
    current=str(current_pid) if type(current_pid) is int and 0<current_pid<(1<<32) else current_pid if isinstance(current_pid,str) and len(current_pid)<=10 and current_pid.isascii() and current_pid.isdecimal() and 0<int(current_pid)<(1<<32) else None
    witnesses=('source-pending-with-hide-skipped','invalid-local-ready-fields','reported-local-xinput-api-unavailable','invalid-native-slot-fields','source-pending-at-log')
    for filename,rows,metadata in members:
        if not re.fullmatch(r'native-(?:lifecycle|detail)\.log(?:\.(?:prev|previous|[0-9]+))?',filename):continue
        for line,row in rows:
            event=row.get('event')
            if type(event) is not str or event not in CONTROLLER_LOCAL_VISIBILITY_EVENTS or event=='input-host-switch-audit' and row.get('beat')!='virtual-ready':continue
            total+=1;record=controller_local_visibility_record(row);counts[record['state']]+=1
            pid=row.get('pid');birth=row.get('writerProcessCreation100ns');pidKnown=type(pid) is int and 0<pid<(1<<32)
            birthKnown=isinstance(birth,str) and 0<len(birth)<=20 and birth.isascii() and birth.isdecimal() and 0<int(birth)<(1<<64)
            relation='unknown-writer-tuple' if not(pidKnown and birthKnown) else 'current-pid-match' if current is not None and str(pid)==current else 'other-or-uncompared'
            key=(relation,event);bank=banks.setdefault(key,{'rows':0,'head':[],'tail':collections.deque(maxlen=2),'witnesses':{}});bank['rows']+=1
            stampValue=row.get('time');stampKnown=isinstance(stampValue,str) and 0<len(stampValue)<=64 and not any(0xd800<=ord(c)<=0xdfff for c in stampValue)
            item={'encounterOrdinal':total,'file':filename[:160],'line':line if type(line) is int and 0<line<(1<<64) else None,
                'writerPid':pid if pidKnown else None,'writerProcessCreation100ns':birth if birthKnown else None,
                'writerTupleReportedNotOsVerified':True,'pidRelation':relation,'event':event,'time':stampValue if stampKnown else None,'record':record}
            if len(bank['head'])<2:bank['head'].append(item)
            else:bank['tail'].append(item)
            kind=record.get('interpretation') if record.get('interpretation') in witnesses else record['state']
            if kind in witnesses:bank['witnesses'].setdefault(kind,item)
    listed={};groups=[]
    for (relation,event),bank in banks.items():
        items=bank['head']+list(bank['tail'])+list(bank['witnesses'].values())
        for item in items:listed[item['encounterOrdinal']]=item
        groups.append({'pidRelation':relation,'event':event,'rows':bank['rows']})
    return {'schemaVersion':1,'scope':'retained-same-row-local-target-and-XInput-observations;not-external-controller-count-or-ownership',
        'rowsObserved':total,'rowsByState':dict(counts),'groups':groups,'maxGroups':12,
        'observations':[listed[i] for i in sorted(listed)],'maxListedObservations':108,'rowsNotListed':total-len(listed),
        'selection':'first2,last2,and-first-of5-fixed-diagnostic-kinds-per-event-per-PID-relation',
        'pidRelationIsLifetimeProof':False,'rowsJoinedToAssertSameHostOrDevice':False,'externalConsumerVisibilityProven':False,
        'uniqueControllerCountProven':False,'controllerDuplicationOrAbsenceProven':False,'completeHistoryProven':False,
        'readingRules':['open-result physicalVisibility is a legacy local policy/admission label;old rows without scope remain unconfirmed and cannot prove physical/external readback',
            'virtual-ready consumerVisible=false is a proof boundary,not observed external invisibility',
            'local target enabled/mounted,physical-hidden report and source-pending are separate same-row fields',
            'same-process XInput observations may have HidHide exemption;one XInput slot cannot exclude an additional HID controller',
            'legacy HID arrays do not report enumeration outcome;complete is the bounded same-row Raw Input sample, not proof of controller absence',
            'retained rows may be duplicate or from different writers/times;this view performs no host/device join or recovery action']}


def native_control_window_record(row):
    result={'state':'unsupported-format','samplerCounters':{'state':'not-inspected','reported':{},'fieldIssues':[]},
        'current':{'state':'not-inspected'},'retired':[],'retiredWindowCountState':'not-inspected','retiredWindowsReportedCount':None,'retiredRowsNotInspected':0,
        'differentWindowCountsCompared':False,'requestsJoined':False,'physicalOrGameDeliveryProven':False}
    if type(row.get('formatVersion')) is not int or row['formatVersion']!=1:return result
    value=row.get('allObservedControls');current=_native_control_window_record(value)
    if 'allObservedControls' not in row:current['state']='missing'
    result['current']=current
    if isinstance(value,dict) and current['state'] in ('reported-window','invalid-fields'):
        retired=value.get('retiredBindingWindows')
        result['retiredWindowCountState']='missing' if 'retiredBindingWindows' not in value else 'null' if retired is None else 'invalid-type'
        if isinstance(retired,list):
            result['retiredWindowCountState']='reported-count' if len(retired)<=2 else 'over-capacity'
            result['retiredWindowsReportedCount']=len(retired);result['retiredRowsNotInspected']=max(0,len(retired)-2)
            result['retired']=[_native_control_window_record(v,False) for v in retired[:2]]
    window=row.get('window');sampler=result['samplerCounters']
    sampler['state']='missing' if 'window' not in row else 'null' if window is None else 'invalid-type'
    if isinstance(window,dict):
        issues=sampler['fieldIssues'];reported=sampler['reported']
        implicit_scope = ('counterScope' not in row and row.get('event') == 'input-native-signal-window' and
                          type(row.get('nativeSignalScopeSchema')) is int and row['nativeSignalScopeSchema'] == 1)
        if not implicit_scope and row.get('counterScope')!=NATIVE_SIGNAL_COUNTER_SCOPE:issues.append('counterScope:unsupported')
        for key in ('observations','attempts','accepted','semanticNeutral','semanticNeutralAccepted','repeatedKnownSourceReads','clockRegressions','selectedSourceKnownReads','selectedSourceLiveReads','selectedSourceNonzeroReads'):
            v=window.get(key)
            if _control_uint(v):reported[key]=v
            else:issues.append(key+':missing-or-invalid')
        for a,b in (('attempts','observations'),('accepted','attempts'),('semanticNeutral','observations'),('semanticNeutralAccepted','accepted'),('semanticNeutralAccepted','semanticNeutral'),('repeatedKnownSourceReads','observations'),('clockRegressions','observations'),('selectedSourceKnownReads','observations'),('selectedSourceLiveReads','selectedSourceKnownReads'),('selectedSourceNonzeroReads','selectedSourceLiveReads')):
            if a in reported and b in reported and reported[a]>reported[b]:issues.append(a+':exceeds-'+b)
        sampler['state']='invalid-fields' if issues else 'reported-sampler-window'
    result['state']='reported-projection' if current['state']=='reported-window' and sampler['state']=='reported-sampler-window' and result['retiredWindowCountState']=='reported-count' and all(v['state']=='reported-window' for v in result['retired']) else 'partial-or-invalid-projection'
    return result


def _native_control_counter_group(row,filename,record):
    writer=row.get('pid');birth=row.get('writerProcessCreation100ns')
    if not (_control_uint(writer,32) and writer>0 and type(birth) is str and 0<len(birth)<=20 and birth.isascii() and birth.isdecimal() and 0<int(birth)<(1<<64)):return None,None
    current=record['current'];fields=current.get('reported',{});sampler=record['samplerCounters'].get('reported',{});source=current.get('selectedSource',{}).get('reported',{})
    signature={'file':filename if len(filename)<=160 else None,'fileNameSha256':hashlib.sha256(filename.encode('utf8')).hexdigest() if len(filename)>160 else None,
        'writerPid':writer,'writerProcessCreation100ns':birth,'projectionState':record['state'],'currentState':current['state'],
        'bindingKnown':fields.get('bindingKnown'),'runId':fields.get('runId'),'persona':fields.get('persona'),'epoch':fields.get('epoch'),'powerGeneration':fields.get('powerGeneration'),
        'observations':fields.get('observations'),'submitAttempts':fields.get('submitAttempts'),'hostAcks':fields.get('hostAcks'),
        'controlEdgeTransitions':fields.get('controlEdgeTransitions'),'nonfiniteValues':fields.get('nonfiniteValues'),'overwrittenOldestControlEdges':fields.get('overwrittenOldestControlEdges'),
        'selectedSourceObservations':source.get('observations'),'selectedSourceLiveObservations':source.get('liveObservations'),
        'samplerObservations':sampler.get('observations'),'samplerAttempts':sampler.get('attempts')}
    return signature,json.dumps(signature,ensure_ascii=False,sort_keys=True,separators=(',',':'))


NATIVE_CONTROL_COUNTER_MARGINAL_FIELDS=(
    ('control','observations'),('control','submitAttempts'),('control','hostAcks'),
    ('control','controlEdgeTransitions'),('control','nonfiniteValues'),('control','overwrittenOldestControlEdges'),
    ('selectedSource','observations'),('selectedSource','liveObservations'),
    ('sampler','observations'),('sampler','attempts'))
NATIVE_CONTROL_COUNTER_MARGINAL_RELATIONS=('current-pid-match','other-or-uncompared','unknown-writer-tuple')


def _native_control_counter_marginal_add(groups,relation,record):
    # Fixed scalar marginals retain coverage after the separate joint-shape bank fills.
    if relation not in groups:
        groups[relation]={'pidRelation':relation,'rows':0,'rowsWithAnyTypedCounter':0,'rowsWithAllTypedCounters':0,
            'partialOrInvalidProjectionRows':0,'fields':[{'scope':scope,'field':field,'reportedValueRows':0,
                'unavailableValueRows':0,'reportedZeroRows':0,'reportedPositiveRows':0,'minimum':None,'maximum':None}
                for scope,field in NATIVE_CONTROL_COUNTER_MARGINAL_FIELDS]}
    group=groups[relation];group['rows']+=1;group['partialOrInvalidProjectionRows']+=record['state']!='reported-projection'
    current=record['current'];views={'control':current.get('reported',{}),
        'selectedSource':current.get('selectedSource',{}).get('reported',{}),'sampler':record['samplerCounters'].get('reported',{})}
    known=0
    for (scope,field),target in zip(NATIVE_CONTROL_COUNTER_MARGINAL_FIELDS,group['fields']):
        value=views[scope].get(field)
        if not _control_uint(value):target['unavailableValueRows']+=1;continue
        known+=1;target['reportedValueRows']+=1;target['reportedZeroRows']+=value==0;target['reportedPositiveRows']+=value>0
        target['minimum']=value if target['minimum'] is None else min(target['minimum'],value)
        target['maximum']=value if target['maximum'] is None else max(target['maximum'],value)
    group['rowsWithAnyTypedCounter']+=known>0;group['rowsWithAllTypedCounters']+=known==len(NATIVE_CONTROL_COUNTER_MARGINAL_FIELDS)


def _native_control_counter_marginal_evidence(groups,total):
    return {'schemaVersion':1,'state':'reported-retained-row-marginals' if total else 'not-observed',
        'scope':'independent-typed-scalar-distributions-across-retained-files-and-writers-per-pid-relation;not-coherent-joint-values',
        'rowsObserved':total,'maxRelations':3,'maxFieldsPerRelation':10,
        'relations':[groups[k] for k in NATIVE_CONTROL_COUNTER_MARGINAL_RELATIONS if k in groups],
        'pidRelationIsLifetimeProof':False,'crossCounterCorrelationProven':False,'counterValuesSummed':False,
        'sourceRowsMayBeDuplicate':True,'uniqueFramesCounted':False,'rawRowsRemoved':False,
        'hardwareLossProven':False,'completeHistoryProven':False,
        'readingRules':['each row enters one fixed PID relation;missing or invalid writer tuple remains a separate unknown relation',
            'unavailable fields remain unknown;zero and positive counts refer to retained rows,not unique frames',
            'typed scalars from partial rows remain marked by partialOrInvalidProjectionRows;they do not certify coherent windows',
            'control,selectedSource and sampler scopes remain separate;independent minima/maxima do not form an observed joint tuple',
            'this view does not expand the original32 joint groups per PID relation or repair missing source history']}


def native_control_window_evidence(members,current_pid):
    head,tail,witnesses=[],collections.deque(maxlen=8),{}
    states=collections.Counter();controls=collections.Counter();sources=collections.Counter();diagnostics=collections.Counter();total=windows=source_zero=0
    ranges={name:{'field':name,'reportedRangeWindows':0,'unknownValueWindows':0,'invalidRangeWindows':0,'minimum':None,'maximum':None} for name in NATIVE_CONTROL_FIELDS}
    counterGroups={};counterGroupBanks=collections.Counter();counterRowsUnknownWriter=counterRowsOverCapacity=0;counterMarginals={}
    for filename,rows,metadata in members:
        if not re.fullmatch(r'native-(?:lifecycle|detail)\.log(?:\.(?:prev|previous|[0-9]+))?',filename):continue
        for line,row in rows:
            if row.get('event')!='input-native-signal-window':continue
            total+=1;record=native_control_window_record(row);states[record['state']]+=1;controls[record['current']['state']]+=1
            groupSignature,groupKey=_native_control_counter_group(row,filename,record)
            relation='unknown-writer-tuple'
            if groupKey is None:counterRowsUnknownWriter+=1
            else:
                relation='current-pid-match' if current_pid is not None and str(row['pid'])==str(current_pid) else 'other-or-uncompared'
                trackedKey=(relation,groupKey)
                if trackedKey in counterGroups:counterGroups[trackedKey]['rows']+=1;counterGroups[trackedKey]['lastEncounterOrdinal']=total
                elif counterGroupBanks[relation]<32:
                    counterGroups[trackedKey]={'pidRelation':relation,'signature':groupSignature,'rows':1,'firstEncounterOrdinal':total,'lastEncounterOrdinal':total};counterGroupBanks[relation]+=1
                else:counterRowsOverCapacity+=1
            _native_control_counter_marginal_add(counterMarginals,relation,record)
            kinds=[];current=record['current'];observed=[current]+record['retired']
            if record['state']=='unsupported-format':kinds.append('unsupported-format')
            if current['state'] in ('missing','null','invalid-type'):kinds.append('missing-or-invalid-control-container')
            if current['state']=='reported-contended':kinds.append('reported-contention')
            if current['state']=='reported-unavailable':kinds.append('reported-unavailable')
            if current['state'] in ('unsupported-schema','unsupported-scope'):kinds.append('unsupported-control-contract')
            if record['samplerCounters']['state']=='invalid-fields':kinds.append('invalid-sampler-counters')
            if record['samplerCounters']['state'] in ('missing','null','invalid-type'):kinds.append('missing-or-invalid-sampler-container')
            if record['retired']:kinds.append('retired-window-present')
            if record['retiredRowsNotInspected']:kinds.append('over-capacity-retired-windows')
            if current['state'] in ('reported-window','invalid-fields') and record['retiredWindowCountState'] in ('missing','null','invalid-type'):kinds.append('missing-or-invalid-retired-window-list')
            for window in observed:
                if window['state'] not in ('reported-window','invalid-fields'):continue
                windows+=1;fields=window['reported']
                selected_source=window['selectedSource'];sources[selected_source['state']]+=1
                if selected_source['state']=='reported-source-window' and selected_source['reported'].get('observations')==0:source_zero+=1
                if selected_source['state']!='reported-source-window' or selected_source['triggerRanges']['state']!='reported-range':kinds.append('source-not-observed-or-invalid')
                if window['state']=='invalid-fields':kinds.append('invalid-control-fields')
                if fields.get('nonfiniteValues',0):kinds.append('reported-nonfinite-values')
                if fields.get('overwrittenOldestControlEdges',0):kinds.append('reported-edge-overwrite')
                if fields.get('overwrittenRetiredBindingWindows',0) or fields.get('lostRetiredBindingObservations',0):kinds.append('reported-retired-loss')
                for field in window['ranges']['fields']:
                    target=ranges[field['field']]
                    if field['state']=='reported-range':
                        target['reportedRangeWindows']+=1
                        target['minimum']=field['minimum'] if target['minimum'] is None else min(target['minimum'],field['minimum'])
                        target['maximum']=field['maximum'] if target['maximum'] is None else max(target['maximum'],field['maximum'])
                    elif field['state']=='unknown-value':target['unknownValueWindows']+=1;kinds.append('unknown-field-values')
                    else:target['invalidRangeWindows']+=1;kinds.append('invalid-range-values')
            if record['state']=='reported-projection':kinds.append('reported-projection')
            ref=(total,filename,line,row,metadata,record)
            if len(head)<8:head.append(ref)
            else:tail.append(ref)
            diagnostics.update(set(kinds))
            for kind in kinds:witnesses.setdefault(kind,ref)
    selected=sorted({r[0]:r for r in head+list(tail)+list(witnesses.values())}.values(),key=lambda r:r[0]);observations=[]
    for ordinal,filename,line,row,metadata,record in selected:
        writer,role=process_identity(row,filename);birth=row.get('writerProcessCreation100ns');time=stamp(row,filename)
        birth_known=isinstance(birth,str) and 0<len(birth)<=20 and birth.isascii() and birth.isdecimal() and 0<int(birth)<(1<<64)
        item={'file':filename[:160],'line':line,'encounterOrdinal':ordinal,'writerPid':None if writer=='unknown' else writer,'writerIdentityField':role,
            'writerProcessCreation100ns':birth if birth_known else None,'writerLifetimeProven':False,
            'currentPidMatch':None if writer=='unknown' or current_pid is None else writer==str(current_pid),
            'time':time if time is None or len(time)<=512 and not any(0xd800<=ord(c)<=0xdfff for c in time) else None,'controlWindow':record}
        if len(filename)>160:item['originalFileNameSha256']=hashlib.sha256(filename.encode('utf8')).hexdigest()
        for key in ('archiveEntryOccurrence','containerOnly'):
            value=metadata.get(key)
            if (key=='archiveEntryOccurrence' and _control_uint(value)) or (key=='containerOnly' and type(value) is bool):item[key]=value
        observations.append(item)
    for field in ranges.values():field['windowsWithoutFieldProjection']=windows-field['reportedRangeWindows']-field['unknownValueWindows']-field['invalidRangeWindows']
    return {'schemaVersion':1,'scope':'bounded-retained-Native-control-window-projections;not-unique-frames-hardware-freshness-or-delivery',
        'rowsObserved':total,'reportedWindowsInspected':windows,'countsByProjectionState':dict(states),'countsByCurrentControlState':dict(controls),
        'countsBySelectedSourceState':dict(sources),'reportedZeroSourceObservationWindows':source_zero,
        'rowCountsByDiagnosticKind':{k:diagnostics[k] for k in sorted(diagnostics)},'diagnosticKindCountsAreDisjoint':False,
        'fieldRangesAcrossRetainedReportedWindows':list(ranges.values()) if windows else [],'fieldRangeProjectionState':'reported-field-windows' if windows else 'not-observed-or-uninterpretable',
        'rangeScope':'union of coherent reported field pairs across retained writers/current-and-retired windows;duplicates do not become unique samples',
        'counterMarginals':_native_control_counter_marginal_evidence(counterMarginals,total),
        'coarseCounterGroups':{'schema':1,'maxTrackedGroups':64,'maxTrackedGroupsPerPidRelation':32,'pidRelationIsLifetimeProof':False,'groups':list(counterGroups.values()),'rowsUnclassifiedUnknownWriter':counterRowsUnknownWriter,'rowsUnclassifiedAfterCapacity':counterRowsOverCapacity,
            'scope':'coarse typed counter shapes per retained file and same-row writer;not full-row equality or unique frame counts','fullRowEqualityProven':False,'deduplicationAuthorized':False,'rawRowsRemoved':False,'completeDistinctGroupCountProven':False},
        'observations':observations,'maxListedObservations':34,'rowsNotListed':total-len(observations),
        'selection':'first8,last8,and-first-of18-fixed-diagnostic-kinds','samplerAndControlCountersSummed':False,'differentWindowsOrRequestsJoined':False,
        'queryOrUniqueApiCallsCounted':False,'sourceDamageOrPhysicalLossProven':False,'completeControlHistoryProven':False,
        'readingRules':['fieldValueKnown=false ignores stored min/max,including zero or stale values;missing/invalid arrays never supply zero',
            'sampler counters may span bindings;control current/retired windows have independent scopes and are never compared or summed into unique frames',
            'source trigger ranges with observations=0 remain unobserved;nonfiniteValues counts reported values,not frames or physical defects',
            'edge overwrite/retired loss/contention are original diagnostic reports;zero/absence does not prove complete capture or source health',
            'retired windows are limited to two and edge observations to16 per inspected window;malformed overflow remains explicit',
            'phase/tuple/clock/driver/game proof requires its own source;this view performs no join,no device IO and no automatic recovery']}


def native_foreground_exit_query_record(row):
    value=row.get('helperExitQuery')
    state='missing' if 'helperExitQuery' not in row else 'null' if value is None else 'object' if isinstance(value,dict) else 'invalid-type'
    result={'state':state,'reported':{},'fieldIssues':[],'reportedQuerySucceeded':None,'exitValueValidatedByReportedQuery':False}
    if state!='object':return result
    if type(value.get('schema')) is not int or value['schema']!=1 or value.get('api')!='GetExitCodeProcess':
        result['state']='unsupported';return result
    fields=result['reported'];issues=result['fieldIssues']
    specs=(('schema',lambda v:type(v) is int and v==1),('api',lambda v:v=='GetExitCodeProcess'),
        ('attempted',lambda v:type(v) is bool),('succeeded',lambda v:v is None or type(v) is bool),
        ('errorDomain',lambda v:v is None or v=='win32-last-error'),
        ('error',lambda v:v is None or type(v) is int and 0<=v<(1<<32)))
    for key,valid in specs:
        if key not in value:issues.append(key+':missing')
        elif not valid(value[key]):issues.append(key+':invalid-type-range-or-value')
        else:fields[key]=value[key]
    if issues:result['state']='invalid-fields';return result
    if not fields['attempted']:
        if any(fields[k] is not None for k in ('succeeded','errorDomain','error')):issues.append('not-attempted:nonnull-result')
        result['state']='contradictory' if issues else 'reported-not-attempted';return result
    if fields['succeeded'] is True:
        if fields['errorDomain'] is not None or fields['error'] is not None:issues.append('successful-query:unexpected-error')
        result['state']='contradictory' if issues else 'reported-query-succeeded'
        if not issues:
            result['reportedQuerySucceeded']=True
            code=row.get('helperExitCode')
            result['exitValueValidatedByReportedQuery']=type(code) is int and 0<=code<(1<<32)
    elif fields['succeeded'] is False:
        if fields['errorDomain']!='win32-last-error' or fields['error'] is None:issues.append('failed-query:missing-error')
        result['state']='contradictory' if issues else 'reported-query-failed'
        if not issues:result['reportedQuerySucceeded']=False
    else:result['state']='contradictory';issues.append('attempted-query:null-result')
    return result


def native_foreground_helper_outcome(row):
    """Only the original same-helper terminal receipt labels an exit report."""
    if row.get('reason') != 'same-helper-terminal-receipt':
        return 'not-same-helper-terminal-receipt'
    code, pending, failure = row.get('helperExitCode'), row.get('helperPending'), row.get('helperFailure')
    if (type(code) is not int or not 0 <= code < (1 << 32) or
            type(pending) is not bool or not isinstance(failure, str)):
        return 'incomplete-terminal-fields'
    if pending:
        return 'conflicting-terminal-fields'
    query=native_foreground_exit_query_record(row)
    if query['state']=='reported-query-failed':
        return 'reported-exit-query-failed' if row.get('phase')=='refresh-failed' and failure=='helper-terminal-nonzero-exit' else 'conflicting-terminal-fields'
    if query['state']=='reported-not-attempted':return 'unproven-exit-query'
    if query['state'] not in ('missing','reported-query-succeeded'):return 'invalid-exit-query-evidence'
    if row.get('phase') == 'refresh-completed' and code == 0 and failure == '':
        return 'reported-zero-exit'
    if row.get('phase') == 'refresh-failed' and code != 0 and failure == 'helper-terminal-nonzero-exit':
        return 'reported-nonzero-exit'
    return 'conflicting-terminal-fields'


FOREGROUND_CYCLE_DEFAULTS = (
    ('coldReturnCycle', dict(coldReturnTicket=0, coldReturnDeadline=0, coldReturnAborted=False,
        coldReturnRefreshSpent=False, coldReturnRefreshAccepted=False, coldReturnPreMutationRejected=False,
        coldReturnPreMutationResumes=0)),
    ('postHideCycle', dict(postHideTicket=0, postHideTerminalTick=0, postHideSourceBaselineSequence=0,
        postHideReadmitted=False, postHideSourceAfterTick=0, postHideDeadline=0, postHideCancellationAvailable=False)),
)


def native_foreground_cycle_details(row):
    """Restore only explicitly encoded defaults; legacy omissions stay unknown."""
    flag_name = 'cycleDetailsDefaultsV1'
    mask = row.get(flag_name, 0)
    mask_valid = type(mask) is int and (mask in (1, 2, 3) if flag_name in row else mask == 0)
    groups = {}
    for bit, (cycle, defaults) in enumerate(FOREGROUND_CYCLE_DEFAULTS):
        active = row.get(cycle)
        known = {k: row[k] for k, default in defaults.items() if k in row and type(row[k]) is type(default)
                 and (type(default) is bool or 0 <= row[k] < (1 << 64))}
        if not mask_valid:
            state = 'invalid-default-mask'
        elif mask & (1 << bit):
            if active is not False or any(k in row for k in defaults):
                state = 'conflicting-default-mask'
            else:
                state, known = 'reported-inactive-defaults', defaults.copy()
        elif type(active) is bool and len(known) == len(defaults):
            state = 'reported-explicit-fields'
        else:
            state = 'missing-or-invalid-fields'
        groups[cycle] = dict(state=state, active=active if type(active) is bool else None, reported=known)
    return groups


def native_foreground_helper_record(row):
    def project(value, spec):
        fields, states = {}, {}
        for key, kind in spec.items():
            if key not in value:
                states[key] = 'missing'; continue
            part = value[key]
            if part is None:
                fields[key] = None; states[key] = 'null'; continue
            valid = (type(part) is bool if kind == 'bool' else
                     type(part) is int and 0 <= part < (1 << 32) if kind == 'uint32' else
                     type(part) is int and -(1 << 31) <= part < (1 << 31) if kind == 'int32' else
                     isinstance(part, str))
            if not valid:
                states[key] = 'invalid-type-or-range'; continue
            if kind == 'text':
                shown = part[:256].encode('utf-8', errors='backslashreplace').decode('utf-8')[:256]
                states[key] = 'bounded-or-escaped-prefix' if shown != part else 'recorded'
                fields[key] = shown
            else:
                fields[key] = part; states[key] = 'recorded'
        return {'reported': fields, 'fieldStates': states}
    def child(value, key, spec):
        part = value.get(key)
        state = 'missing' if key not in value else 'null' if part is None else 'object' if isinstance(part, dict) else 'invalid-type'
        return {'containerState': state, **project(part if isinstance(part, dict) else {}, spec)}
    writer = child(row, 'nativeRefreshWriter', {'phase':'text','settled':'bool','reserved':'bool',
        'error':'uint32','attemptId':'text','childPid':'uint32','parentPid':'uint32'})
    diagnostic = child(row, 'helperDiagnostic', {'state':'text','scope':'text','hardwareSuccessProven':'bool'})
    parent = row.get('helperDiagnostic'); parent = parent if isinstance(parent, dict) else {}
    diagnostic['reportedFacts'] = child(parent, 'reportedFacts', {'Schema':'uint32','Kind':'text','Stage':'text',
        'Result':'uint32','DisableResult':'uint32','EnableResult':'uint32','DisableCallEntered':'bool',
        'EnableCallEntered':'bool','ExceptionType':'text','ExceptionHResult':'int32'})
    result = {'helperOutcome': native_foreground_helper_outcome(row),'exitQuery':native_foreground_exit_query_record(row),
        'terminalReport': project(row, {'phase':'text','reason':'text','helperPending':'bool',
            'helperExitCode':'uint32','helperFailure':'text','helperError':'uint32'}),
        'outerState': project(row, {'refreshFailed':'bool','cycleAccepted':'bool','configurationReturned':'bool'}),
        'writerSettlement': writer, 'helperDiagnostic': diagnostic}
    if 'cycleDetailsDefaultsV1' in row or 'coldReturnCycle' in row or 'postHideCycle' in row:
        result['cycleDetails'] = native_foreground_cycle_details(row)
    return result


class _ForegroundPhaseSnapshots:
    """Bounded nonterminal row references;phase text does not authorize an action."""
    def __init__(self):
        self.total=0
        self.head=[]
        self.tail=collections.deque(maxlen=4)
        self.phases={}
        self.witnesses={}
        self.pending=collections.Counter()
        self.queries=collections.Counter()
        self.phase_states=collections.Counter()
        self.untracked=0

    def observe(self,filename,line,row,metadata):
        self.total+=1
        phase=row.get('phase')
        phase_state=('missing' if 'phase' not in row else 'null' if phase is None else
            'reported-text' if isinstance(phase,str) and 0<len(phase)<=96 and
            not any(0xd800<=ord(c)<=0xdfff for c in phase) and len(phase.encode('utf8'))<=96 else 'invalid-type-or-length')
        value=row.get('helperPending')
        pending=('missing' if 'helperPending' not in row else 'null' if value is None else
            'reported-true' if value is True else 'reported-false' if value is False else 'invalid-type')
        query=native_foreground_exit_query_record(row)['state']
        self.pending[pending]+=1;self.queries[query]+=1;self.phase_states[phase_state]+=1
        ref=(self.total,filename,line,row,metadata,phase_state,pending)
        if len(self.head)<4:self.head.append(ref)
        else:self.tail.append(ref)
        self.witnesses.setdefault(('pending',pending),ref)
        self.witnesses.setdefault(('query',query),ref)
        self.witnesses.setdefault(('phase-shape',phase_state),ref)
        if phase_state=='reported-text':
            key=(row['event'],phase)
            if key not in self.phases and len(self.phases)<16:
                self.phases[key]={'event':row['event'],'phase':phase,'rows':0,'first':ref}
            if key in self.phases:self.phases[key]['rows']+=1
            else:self.untracked+=1

    def finish(self):
        refs=self.head+list(self.tail)+list(self.witnesses.values())+[p['first'] for p in self.phases.values()]
        selected=sorted({r[0]:r for r in refs}.values(),key=lambda r:r[0])
        observations=[]
        for ordinal,filename,line,row,metadata,phase_state,pending in selected:
            writer,role=process_identity(row,filename);birth=row.get('writerProcessCreation100ns');time=stamp(row,filename)
            birth_known=isinstance(birth,str) and 0<len(birth)<=20 and birth.isascii() and birth.isdecimal() and 0<int(birth)<(1<<64)
            snapshot=native_foreground_helper_record(row);snapshot.pop('helperOutcome',None)
            item={'file':filename[:160],'line':line,'encounterOrdinal':ordinal,'event':row['event'],
                'phaseTextState':phase_state,'helperPendingState':pending,'terminalReceipt':False,
                'writerPid':None if writer=='unknown' else writer,'writerIdentityField':role,
                'writerProcessCreation100ns':birth if birth_known else None,'writerLifetimeProven':False,
                'time':time if time is None or len(time)<=512 and not any(0xd800<=ord(c)<=0xdfff for c in time) else None,
                'reportedSnapshot':snapshot}
            if len(filename)>160:item['originalFileNameSha256']=hashlib.sha256(filename.encode('utf8')).hexdigest()
            for key in ('archiveEntryOccurrence','containerOnly'):
                value=metadata.get(key)
                if (key=='archiveEntryOccurrence' and type(value) is int and 0<=value<(1<<64)) or (key=='containerOnly' and type(value) is bool):item[key]=value
            observations.append(item)
        return {'schemaVersion':1,'scope':'retained-nonterminal-foreground-phase-snapshots;not-operation-or-live-process-proof',
            'rowsObserved':self.total,'countsByPendingState':dict(self.pending),'countsByExitQueryState':dict(self.queries),
            'countsByPhaseTextState':dict(self.phase_states),
            'trackedReportedPhases':[{k:v for k,v in p.items() if k!='first'} for p in self.phases.values()],
            'maxTrackedReportedPhases':16,'eligiblePhaseRowsNotTracked':self.untracked,
            'observations':observations,'rowsNotListed':self.total-len(observations),'maxListedObservations':42,
            'selection':'first4,last4,first16-reported-event-phase-types,and-first-of-five-pending/nine-exit-query/four-phase-shape-states',
            'sameAttemptRowsJoined':False,'uniqueOperationsCounted':False,'observedPhaseNamesAreAuthorizedStates':False,
            'pendingFlagProvesLiveProcess':False,'newExitQueriesCounted':False,'currentRecoveryOrPhysicalSuccessProven':False,
            'readingRules':['refresh-completed/refresh-failed remain in the original terminal view only;other phase rows are snapshots',
                'reported phase text is opaque;bounded type counts cover retained rows across writers/rotations/occurrences,not unique operations',
                'pending=true is a producer flag;pending=false or a stored exit code is not independent process-lifetime or fresh API evidence',
                'exit-query metadata may repeat between snapshots;query-state counts are row counts,not new GetExitCodeProcess calls',
                'missing,null,malformed and untracked phase rows stay explicit;omitted phase rows do not count distinct phase names',
                'original reason,writer settlement,helper diagnostic and outer flags remain separate;no raw private payload or full configuration is copied']}


def native_bus_rate_scope_supported(row, is_sample):
    if 'busRateSampleScope' not in row:
        return row.get('dispatch') == 'direct-timer-callback'
    if not is_sample or type(row['busRateSampleScope']) is not int or row['busRateSampleScope'] != 1:
        return False
    return all(key not in row or type(row[key]) is type(value) and row[key] == value for key, value in (
        ('dispatch', 'direct-timer-callback'), ('highRateThresholdMultiplier', 2)))


def native_bus_rate_evidence(members):
    """Summarize retained BUS windows and transitions without joining the lanes."""
    pattern = re.compile(r'native-(lifecycle|detail)(?:.log(?:.(?:prev|previous|[0-9]+))?|.(?:prev|previous|[0-9]+).log)')
    window_event, change_event = 'input-capture-bus-rate-sample', 'input-capture-bus-rate'
    counts, rejected, samples, transitions = (collections.Counter() for _ in range(4))
    groups, witnesses = {}, {}

    def uint(value, maximum=(1 << 64) - 1):
        return type(value) is int and 0 <= value <= maximum

    def save_witness(key, filename, line, occurrence, row, flags):
        if key in witnesses:
            return
        witnesses[key] = dict(file=filename, line=line, archiveEntryOccurrence=occurrence,
                              event=row['event'], rateFlags=flags, tickCount=row['tickCount'],
                              busySkips=row['busySkips'], windowMs=row['windowMs'],
                              expectedPublishHz=row['expectedPublishHz'],
                              previousRateFlags=row.get('previousRateFlags') if row['event'] == change_event else None)

    for filename, rows, metadata in members:
        carrier = pattern.fullmatch(filename)
        if not carrier or 'container' in metadata or metadata.get('containerOnly') is True:
            continue
        occurrence = metadata.get('archiveEntryOccurrence')
        occurrence = occurrence if uint(occurrence) else None
        for line, row in rows:
            event = row.get('event')
            if event not in (window_event, change_event):
                continue
            is_sample = event == window_event
            counts['sampleRows' if is_sample else 'transitionRows'] += 1
            if carrier[1] != ('detail' if is_sample else 'lifecycle'):
                counts['unexpectedLaneRows'] += 1
            flags = row.get('rateFlags')
            required = ('tickCount', 'busySkips', 'windowMs', 'expectedPublishHz')
            if not uint(flags, 7) or not all(uint(row.get(key)) for key in required):
                rejected['missing-or-invalid-window-fields'] += 1
                continue
            if not native_bus_rate_scope_supported(row, is_sample) or not row['windowMs'] or not row['expectedPublishHz']:
                rejected['unsupported-window-scope'] += 1
                continue
            if (row.get('excessiveCallbackRate') is not bool(flags & 4) or bool(row['busySkips']) != bool(flags & 1)
                    or (flags & 2 and not row['tickCount'])):
                rejected['inconsistent-rate-flags'] += 1
                continue
            if is_sample:
                kind = 'zero-callback-window' if row['tickCount'] == 0 else 'nonzero-callback-window'
                samples[flags, kind] += 1
                save_witness(('window', flags, kind), filename, line, occurrence, row, flags)
                subtype = kind
            else:
                previous = row.get('previousRateFlags')
                if not uint(previous, 7) or previous == flags or row.get('state') != ('abnormal' if flags else 'recovered'):
                    rejected['invalid-transition'] += 1
                    continue
                subtype = 'recovered' if not flags else 'entered' if not previous else 'category-changed'
                transitions[previous, flags, subtype] += 1
                save_witness(('transition', subtype), filename, line, occurrence, row, flags)

            pid, birth, power = row.get('pid'), row.get('writerProcessCreation100ns'), row.get('powerGeneration')
            if (not uint(pid, 0xffffffff) or not pid or not isinstance(birth, str) or
                    not re.fullmatch(r'[1-9][0-9]{0,19}', birth) or not uint(int(birth)) or not uint(power)):
                counts['unboundRows'] += 1
                continue
            key = pid, birth, power
            group = groups.get(key)
            if group is None:
                if len(groups) == 32:
                    counts['writerPowerGroupLimitRows'] += 1
                    continue
                group = groups[key] = dict(pid=pid, writerProcessCreation100ns=birth, powerGeneration=power,
                                           sampleRows=0, flaggedSamples=0, zeroCallbackSamples=0, transitionRows=0)
            group['sampleRows' if is_sample else 'transitionRows'] += 1
            if is_sample:
                group['flaggedSamples'] += bool(flags)
                group['zeroCallbackSamples'] += subtype == 'zero-callback-window'

    return dict(schemaVersion=1, scope='retained-direct-timer-callback-records;sample-rows-and-category-transitions-counted-separately',
                observedSamples=counts['sampleRows'], observedTransitions=counts['transitionRows'],
                unexpectedLaneRows=counts['unexpectedLaneRows'], unboundRows=counts['unboundRows'],
                writerPowerGroupLimitRows=counts['writerPowerGroupLimitRows'], writerPowerGroupLimit=32,
                unprojectedRows=dict(rejected),
                sampleCounts=[dict(rateFlags=flags, kind=kind, count=count) for (flags, kind), count in sorted(samples.items())],
                transitionCounts=[dict(previousRateFlags=previous, rateFlags=flags, kind=kind, count=count)
                                  for (previous, flags, kind), count in sorted(transitions.items())],
                writerPowerGroups=list(groups.values()), witnesses=list(witnesses.values()),
                rateFlagBits={'busySkip':1, 'positiveTickCountBelowThreshold':2, 'excessiveCallbackRate':4},
                completeTimeCoverageProven=False, sampleToTransitionJoinProven=False, physicalInputOrCpuCauseProven=False)


def native_foreground_helper_evidence(members, current_pid):
    """Bounded terminal rows; later snapshots never become extra completions."""
    head, tail, first_outcomes = [], collections.deque(maxlen=8), {}
    counts = collections.Counter(); observed = nonterminal = 0
    phases=_ForegroundPhaseSnapshots()
    for filename, rows, metadata in members:
        if not re.fullmatch(r'native-(?:lifecycle|detail)(?:\.log(?:\.(?:prev|previous|\d+))?|\.(?:prev|previous|\d+)\.log)', filename):
            continue
        for line, row in rows:
            if row.get('event') not in ('native-foreground-isolation', 'msi-native-foreground-isolation'):
                continue
            if row.get('phase') not in ('refresh-completed', 'refresh-failed'):
                nonterminal += 1; phases.observe(filename,line,row,metadata); continue
            observed += 1; outcome = native_foreground_helper_outcome(row); counts[outcome] += 1
            ref = (observed, filename, line, row, metadata)
            if len(head) < 8:
                head.append(ref)
            else:
                tail.append(ref)
            first_outcomes.setdefault(outcome, ref)
    selected = sorted({ref[0]: ref for ref in head + list(tail) + list(first_outcomes.values())}.values(), key=lambda ref:ref[0])
    observations = []
    for ordinal, filename, line, row, metadata in selected:
        writer, role = process_identity(row, filename); birth = row.get('writerProcessCreation100ns')
        birth_known = (isinstance(birth, str) and 0 < len(birth) <= 20 and birth.isascii() and
            birth.isdecimal() and 0 < int(birth) < (1 << 64))
        time = stamp(row, filename)
        observations.append({'file':filename,'line':line,**metadata,'encounterOrdinal':ordinal,'event':row['event'],
            'writerPid':None if writer == 'unknown' else writer,'writerIdentityField':role,
            'writerProcessCreation100ns':birth if birth_known else None,
            'currentPidMatch':None if writer == 'unknown' or current_pid is None else writer == str(current_pid),
            'time':time if time is None or len(time) <= 512 and not any(0xD800 <= ord(c) <= 0xDFFF for c in time) else None,
            **native_foreground_helper_record(row)})
    return {'schemaVersion':1,'scope':'retained-original-foreground-helper-terminal-reports-only',
        'terminalRowsObserved':observed,'otherPhaseRowsNotInterpretedAsTerminals':nonterminal,
        'nonterminalPhaseEvidence':phases.finish(),
        'countsByHelperOutcome':dict(counts),'observations':observations,'rowsNotListed':observed-len(observations),
        'maxListedObservations':24,'selection':'first8,last8,first-of-eight-fixed-helper-outcomes;file-encounter-order',
        'uniqueAttemptsOrFailuresCounted':False,'otherPhaseSnapshotsJoined':False,'exitQuerySuccessProven':False,
        'writerSettlementIsHelperSuccess':False,'helperZeroExitProvesDeviceOrInputSuccess':False,
        'readingRules':['only refresh-completed/refresh-failed with the original terminal reason classify a helper exit report',
            'helperError,helperExitCode,outer refreshFailed and writer settled remain separate reported fields',
            'helperExitQuery is optional original query evidence;failed or unattempted queries do not validate the reported exit value;legacy absence stays unknown',
            'missing,null,invalid types and contradictions stay explicit;pending or unrelated phase snapshots are not extra terminal failures',
            'nonzero is the producer reported exit value;exit-query success,loaded image,full resource return and device effects are not proven',
            'rotations and repeated archive occurrences stay separate;bounded prefixes retain raw file and line references']}



HTTP_FAILURE_PHASES = {
    'unknown': (None,), 'invalid-request': ('url','scheme','method','request-headers'),
    'transport-setup-failed': ('session','connect','request','callback','context'),
    'request-headers-rejected': ('request-headers',), 'response-limit': ('body',),
    'cancelled': ('async-request',), 'power-admission-refused': ('before-transport','before-send','write-grant'),
    'mock-admission-refused': ('before-transport',), 'opaque-exception': (None,),
    'deadline-exceeded': ('queue','send','headers','body'), 'winhttp-failed': ('send','headers','body')}
HTTP_SETUP_APIS = {'session':'WinHttpOpen','connect':'WinHttpConnect','request':'WinHttpOpenRequest',
    'callback':'WinHttpSetStatusCallback','context':'WinHttpSetOption'}


FINITE_NATIVE_EVENTS = (
    'shortcut-action-request','shortcut-action-result','msi-claw-return-debt',
    'steam-overlay-startup','process-efficiency-startup','input-module-replay-unverified',
    'hwinfo-recovery-launch','hwinfo-recovery-result-discarded','controller-motion-raw-registration',
    'gyro-power-early-pause-completed','gyro-power-start-current','fxsound-toggle',
    'settings-upgrade-repaired','gamepad-feedback-stop-pending')
FINITE_SHORTCUT_ACTIONS = ('os.switchProgram','monitor.toggle','display.frameRateControl',
    'fan.switchPreset','app.launchQuickApp','power.tdpAdjust')


TOUCHPAD_EXPORT_SNAPSHOT = 'screen-touchpads-export-state.json'
TOUCHPAD_RECOVERY_STATUSES = ('idle','checking','healthy','recovered','failed','expired','disabled','context-changed','suspended','stopped','timer-failed','exit')
TOUCHPAD_FAILURE_STAGES = (
    'none','attach-channel-not-released','attach-child-window','attach-composition-visual','attach-context','attach-controlled-input',
    'attach-desktop-target','attach-input-controller','attach-input-events','attach-input-source','attach-owned-channel-start',
    'attach-owned-client-rect','attach-owned-command','attach-owned-compositor-interop','attach-owned-controlled-input',
    'attach-owned-desktop-target','attach-owned-independent-composition','attach-owned-input-controller','attach-owned-input-events',
    'attach-owned-input-slot','attach-owned-input-source','attach-owned-input-transparency','attach-owned-process-events',
    'attach-owned-target-root','attach-owned-visual','attach-quiesced','attach-root-style','input-channel-overflow',
    'input-channel-overflow-cancel','mouse-pass-alpha-upload','mouse-pass-context','mouse-pass-controller','mouse-pass-geometry',
    'mouse-pass-quiesced','mouse-pass-style','mouse-pass-verify','runtime-initialize','sync-cancel-pending','sync-composition-visual',
    'sync-context','sync-geometry','sync-independent-input-channel','sync-input-dispatch','sync-input-process-events','sync-quiesced',
    'sync-rebind-context','sync-rebind-geometry','transparency-context','transparency-style')


def touchpad_recovery_record(value, snapshot=False):
    """Typed fields at their original levels; reported recovery never certifies input."""
    if not isinstance(value,dict):return {'state':'invalid-document','reported':{},'fieldStates':{},'fieldIssues':['document:invalid-type']}
    fields={};states={};issues=[];objects=0;projected=0
    def scalar(obj,key,prefix,kind='u32',choices=()):
        nonlocal projected
        path=prefix+key
        if key not in obj:states[path]='missing';return None
        item=obj[key]
        if item is None:state='null'
        elif kind=='bool':state='known' if type(item) is bool else 'invalid-type-range'
        elif kind in ('u32','u64'):state='known' if type(item) is int and 0<=item<(1<<(32 if kind=='u32' else 64)) else 'invalid-type-range'
        else:state='known' if isinstance(item,str) and item in choices else 'unsupported-value' if isinstance(item,str) else 'invalid-type-range'
        states[path]=state
        if state!='known':issues.append(path+':'+state);return None
        fields[path]=item;projected+=1;return item
    def obj(parent,key,prefix):
        nonlocal objects
        path=prefix+key
        if key not in parent:states[path]='missing';return {}
        item=parent[key];state='null' if item is None else 'known' if isinstance(item,dict) else 'invalid-type-range';states[path]=state
        if state!='known':issues.append(path+':'+state);return {}
        objects+=1;return item
    version=scalar(value,'schemaVersion','','u32') if snapshot else None
    if snapshot and version!=1:
        return {'state':'unsupported-document-schema' if version is not None else 'unqualified-document-schema',
            'reported':fields,'fieldStates':states,'fieldIssues':issues,'reportedSignals':[],
            'currentInputHealthProven':False,'rootCauseProven':False}
    state=obj(value,'state','');recovery=obj(value,'resumeRecovery' if snapshot else 'recovery','')
    for key in ('ok','available','enabled','visible','autoHidden','nativeInputChannelIsolated'):scalar(state,key,'state.','bool')
    for key in ('error','nativeInputChannelThread'):scalar(state,key,'state.')
    scalar(state,'events','state.','u64')
    visibility=obj(state,'buttonVisibility','state.');vversion=scalar(visibility,'version','visibility.')
    visibility_supported=vversion in (1,2)
    if visibility and vversion not in (1,2):issues.append('visibility.version:unsupported-contract')
    masks={}
    if visibility_supported:
        for key in ('permittedMask','inputVisibleMask','artworkVisibleMask'):
            masks[key]=scalar(visibility,key,'visibility.')
            if masks[key] is not None and masks[key]>7:issues.append('visibility.'+key+':unsupported-bits');masks[key]=None
    history=obj(visibility,'inputRecovery','visibility.')
    for key in ('aborts','failed','lastError','recovered','retries'):scalar(history,key,'inputRecovery.','u64' if key!='lastError' else 'u32')
    # lastError/failed may survive recovery. They are retained observations,
    # never a count of independent current failures or proof of current health.
    rversion=scalar(recovery,'version','recovery.');status=None
    if recovery and rversion!=1:issues.append('recovery.version:unsupported-contract')
    if rversion==1:
        status=scalar(recovery,'status','recovery.','enum',TOUCHPAD_RECOVERY_STATUSES)
        for key in ('pending','timerArmed'):scalar(recovery,key,'recovery.','bool')
        for key in ('checks','repairs','clean','runtimeRebuilds','inputChannelResets','lastError'):scalar(recovery,key,'recovery.')
        scalar(recovery,'powerGeneration','recovery.','u64')
        budget=obj(recovery,'faultBudget','recovery.')
        for key in ('generation','suspendSerial'):scalar(budget,key,'budget.','u64')
        for key in ('repairs','episodes','runtimeRebuilds','inputChannelResets','maximumRepairs','maximumEpisodes'):scalar(budget,key,'budget.')
    else:budget={}
    failure=obj(recovery,'nativeInputFailure','recovery.') if rversion==1 else {}
    if not failure:failure=obj(state,'nativeInputFailure','state.')
    fversion=scalar(failure,'version','failure.')
    if failure and fversion!=1:issues.append('failure.version:unsupported-contract')
    if fversion==1:
        scalar(failure,'sequence','failure.','u64');scalar(failure,'surface','failure.','enum',('button','pad'))
        scalar(failure,'index','failure.');scalar(failure,'error','failure.');scalar(failure,'stage','failure.','enum',TOUCHPAD_FAILURE_STAGES)
    contradictions=[]
    if status in ('failed','healthy','recovered','expired','disabled') and fields.get('recovery.pending') is True:contradictions.append('terminal-status-with-pending')
    if status=='checking' and fields.get('recovery.pending') is False:contradictions.append('checking-without-pending')
    # Only same-row, supported-version arithmetic can be contradictory.
    for key in ('repairs','episodes'):
        used=fields.get('budget.'+key);limit=fields.get('budget.maximum'+key.title())
        if used is not None and limit is not None and used>limit:contradictions.append('budget.'+key+'-exceeds-reported-maximum')
    signals=[]
    if not contradictions and status=='failed':signals.append('reported-recovery-failed')
    if not contradictions and status=='expired':signals.append('reported-recovery-expired')
    if not contradictions and status in ('healthy','recovered'):signals.append('reported-recovery-'+status)
    if not contradictions and status=='checking':signals.append('reported-recovery-checking')
    if fields.get('inputRecovery.failed',0)>0 or fields.get('inputRecovery.lastError',0)>0:signals.append('retained-input-failure-history')
    if visibility_supported and masks.get('permittedMask',0) and masks.get('inputVisibleMask')==0 and masks.get('artworkVisibleMask')==0:signals.append('reported-permitted-windows-not-visible')
    kinds=set(issue.split(':')[-1] for issue in issues)
    classification='contradictory-fields' if contradictions else 'invalid-fields' if 'invalid-type-range' in kinds else 'unsupported-values' if any('unsupported' in kind for kind in kinds) else 'null-fields' if 'null' in kinds else 'reported-fields'
    return {'state':classification,'reported':fields,'fieldStates':states,'fieldIssues':issues+contradictions,'reportedSignals':signals,
        'topHealthyFlagsOverrideRecovery':False,'currentInputHealthProven':False,'rootCauseProven':False,
        'countersAreUniqueFaultsOrAttempts':False,'physicalVisibilityProven':False,
        'originalRecoveryRowsReconstructed':False,'projectedScalars':projected,'objectsInspected':objects,
        'uninspectedRootFields':len(value)-sum(key in value for key in ('schemaVersion','state','resumeRecovery' if snapshot else 'recovery','event'))}


def touchpad_recovery_evidence(members,current_pid,snapshot=None,selection=None):
    head=[];tail=collections.deque(maxlen=8);witnesses={};total=untracked=0;signals=collections.Counter();classes=collections.Counter()
    for filename,rows,metadata in members:
        if not re.fullmatch(r'native-(?:lifecycle|detail)(?:\.log(?:\.(?:prev|previous|[0-9]+))?|\.(?:prev|previous|[0-9]+)\.log)',filename):continue
        for line,row in rows:
            if not isinstance(row,dict) or row.get('event')!='screen-touchpads-state':continue
            record=touchpad_recovery_record(row);total+=1;classes[record['state']]+=1;signals.update(record['reportedSignals'])
            writer,role=process_identity(row,filename);birth=row.get('writerProcessCreation100ns')
            known=isinstance(birth,str) and 0<len(birth)<=20 and birth.isascii() and birth.isdecimal() and 0<int(birth)<(1<<64)
            relation='writerPidUnknown' if writer=='unknown' else 'currentPidUnspecified' if current_pid is None else 'currentPidMatch' if writer==str(current_pid) else 'otherPid'
            ref={'file':filename[:160],'line':line,'encounterOrdinal':total,'writerPid':None if writer=='unknown' else writer,
                'writerIdentityField':role,'pidRelation':relation,'writerProcessCreation100ns':birth if known else None,
                'writerBirthState':'reported-decimal' if known else 'missing' if 'writerProcessCreation100ns' not in row else 'null' if birth is None else 'invalid',
                'record':record}
            for key in ('archiveEntryOccurrence','containerOnly'):
                item=metadata.get(key)
                if key=='archiveEntryOccurrence' and type(item) is int and 0<=item<(1<<64) or key=='containerOnly' and type(item) is bool:ref[key]=item
            if len(head)<8:head.append(ref)
            else:tail.append(ref)
            key=(record['state'],tuple(record['reportedSignals']))
            if key not in witnesses:
                if len(witnesses)<32:witnesses[key]=ref
                else:untracked+=1
    observations=sorted({q['encounterOrdinal']:q for q in head+list(tail)+list(witnesses.values())}.values(),key=lambda q:q['encounterOrdinal'])
    selection=selection if isinstance(selection,dict) else {};snapshot_state=selection.get('status','not-supplied')
    snapshot_view={'selectionStatus':snapshot_state,'writerBindingProven':False,'joinedToNativeRows':False,'record':None}
    if snapshot_state=='selected':snapshot_view['record']=touchpad_recovery_record(snapshot,snapshot=True)
    return {'schemaVersion':1,'scope':'retained-touchpad-native-state-and-explicit-export-snapshot;not-input-health-or-cause',
        'rowsObserved':total,'countsByState':dict(classes),'rowCountsByReportedSignal':dict(signals),
        'observations':observations,'maxListedObservations':48,'rowsNotListed':total-len(observations),
        'maxClassificationWitnesses':32,'classificationWitnessRowsNotTracked':untracked,'exportSnapshot':snapshot_view,
        'selection':'first8,last8,first32 classification/signal witnesses;encounter order only',
        'topHealthyFlagsOverrideRecovery':False,'currentInputHealthProven':False,'rootCauseProven':False,
        'counterRowsAreUniqueFaultsOrOperations':False,'filesOrSnapshotsJoined':False,'runtimeSourceOrLoadedProven':False,
        'readingRules':['top-level ok/error/available are separate producer fields;they cannot override failed/expired recovery',
            'lastError/failed are retained counters and can survive recovery;never count unique current faults or infer current health',
            'visible,autoHidden and input/artwork/permitted masks retain their own layer;no pixel or OS root-cause inference',
            'disabled profile enabled=false does not mean all standalone buttons are disabled',
            'thread number is reported only,not process identity or proof a thread is alive;missing fields stay unknown',
            'exact root snapshot selected once under existing metadata CRC/duplicate/key/byte rules;no winner from duplicates',
            'native row writer birth must be same-row exact decimal;export snapshot has no invented writer or cross-file terminal join']}


UI_SURFACE_PHASES=('preparing','presented','surface-return','hidden','hide-request')
UI_WEBVIEW_EVENTS=('browser-process-exited-handler','browser-process-exited-normal',
    'process-failed','process-failed-stale-generation')


def ui_presentation_record(row,filename):
    """Original UI reports only. Registration, presentation, and process failure differ."""
    if not isinstance(row,dict):return None
    native=bool(re.fullmatch(r'native-(?:lifecycle|detail)\.log(?:\.(?:prev|previous|[0-9]+))?',filename))
    webview=bool(re.fullmatch(r'webview-failures\.log(?:\.(?:prev|previous|[0-9]+))?',filename))
    event=row.get('event')
    if not ((native and event in ('game-overlay-surface','webview-browser-process-failed')) or
            (webview and event in UI_WEBVIEW_EVENTS)):return None
    fields={};states={};issues=[]
    def take(key,kind,required=False,choices=()):
        if key not in row:
            if required:states[key]='missing';issues.append(key+':missing')
            return None
        value=row[key]
        if value is None:state='null'
        elif kind=='bool':state='known' if type(value) is bool else 'invalid-type-range'
        elif kind=='u64':state='known' if type(value) is int and 0<=value<(1<<64) else 'invalid-type-range'
        elif kind=='u32':state='known' if type(value) is int and 0<=value<(1<<32) else 'invalid-type-range'
        elif kind=='i32':state='known' if type(value) is int and -(1<<31)<=value<(1<<31) else 'invalid-type-range'
        else:state='known' if isinstance(value,str) and value in choices else 'unsupported-value' if isinstance(value,str) else 'invalid-type-range'
        if state!='known':states[key]=state;issues.append(key+':'+state);return None
        fields[key]=value;return value
    signal=None
    if event=='game-overlay-surface':
        phase=take('phase','enum',True,UI_SURFACE_PHASES)
        for key in ('controllerReady','panelReady','displayInert','displayReceiverSeparated','independentSurface',
                'pointerInteractive','screenVisibleVerified','sameForeground','originalParentRestored',
                'carrierVisible','originalMainVisible','panelRetired'):take(key,'bool')
        take('hresult','i32')
        take('reason','enum',choices=('preparing','focus-session-cleared','original-hide-action','original-close-action',
            'summon-toggle','webview-controller-close','explicit-native-dialog-handoff','native-lifecycle','unexpected-native-activation'))
        take('entrypoint','enum',choices=('ipc.window.hide','ipc.window.minimize','ipc.window.close','hideWindowAnimated'))
        signal={'preparing':'reported-preparing','presented':'reported-presentation',
            'surface-return':'reported-surface-return','hidden':'reported-hidden','hide-request':'reported-hide-request'}.get(phase)
    elif event=='browser-process-exited-handler':
        registered=take('registered','bool',True);hr=take('hresult','i32',True)
        signal='reported-handler-registration'
        if registered is not None and hr is not None and registered!=(hr>=0):issues.append('registered:hresult-conflict')
    elif event=='browser-process-exited-normal':
        take('browserProcessId','u32',True);signal='reported-browser-normal-exit'
    elif event=='webview-browser-process-failed':
        take('attempt','u64',True);take('exitCode','i32',True);signal='reported-native-browser-failure'
    else:
        take('kind','enum',True,('browser','renderer','renderer-unresponsive','frame-renderer','utility','sandbox-helper',
            'gpu','ppapi-plugin','ppapi-broker','unknown'))
        for key in ('kindCode','reasonCode','exitCode'):take(key,'i32')
        take('reason','enum',choices=('unexpected','unresponsive','terminated','crashed','launch-failed','out-of-memory','profile-deleted','unknown'))
        take('browserProcessId','u32');take('fromBrowserProcessExited','bool')
        signal='reported-stale-generation-failure' if event=='process-failed-stale-generation' else 'reported-process-failure'
    for key in ('generation','currentGeneration'):take(key,'u64')
    state=('contradictory-fields' if any(x.endswith('conflict') for x in issues) else
        'invalid-fields' if any(x.endswith('invalid-type-range') for x in issues) else
        'unsupported-values' if any(x.endswith('unsupported-value') for x in issues) else
        'null-fields' if any(x.endswith('null') for x in issues) else
        'missing-fields' if issues else 'reported-fields')
    return {'state':state,'event':event,'reported':fields,'fieldStates':states,'fieldIssues':issues,
        'reportedSignal':signal if state=='reported-fields' else None,
        'handlerRegistrationIsExit':False,'presentationProvesPixelsOrInput':False,
        'hiddenMeansProcessExited':False,'processFailureMeansYmccExited':False}


def overlay_renderer_probe_evidence(members):
    """Correlate retained probe reports; the script does not test input or pixels."""
    phases = ('renderer-requested', 'renderer-replied', 'renderer-reply-invalid')
    max_keys, max_examples = 4096, 16
    counts = collections.Counter()
    entries = {}

    def uint(value, bits, minimum=0):
        return type(value) is int and minimum <= value < (1 << bits)

    for filename, rows, metadata in members:
        if not re.fullmatch(r'native-(?:lifecycle|detail)\.log(?:\.(?:prev|previous|[0-9]+))?', filename):
            continue
        for line, row in rows:
            phase = row.get('phase')
            if row.get('event') != 'game-overlay-surface' or phase not in phases:
                continue
            counts[phase] += 1
            pid, birth = row.get('pid'), row.get('writerProcessCreation100ns')
            session, tick = row.get('session'), row.get('requestedTick')
            if (not uint(pid, 32, 1) or not isinstance(birth, str) or
                    not re.fullmatch(r'[1-9][0-9]{0,19}', birth) or int(birth) >= 1 << 64 or
                    not uint(session, 64, 1) or not uint(tick, 64)):
                counts['rowsWithoutCompleteKey'] += 1
                continue
            key = pid, birth, session, tick
            entry = entries.get(key)
            if entry is None:
                if len(entries) >= max_keys:
                    counts['rowsBeyondKeyLimit'] += 1
                    continue
                entry = entries[key] = {'requestCount': 0, 'replyCount': 0, 'request': None, 'reply': None}
            kind = 'request' if phase == 'renderer-requested' else 'reply'
            entry[kind+'Count'] += 1
            if entry[kind] is not None:
                continue
            hr = row.get('hresult')
            hr = hr if type(hr) is int and -(1 << 31) <= hr < 1 << 31 else None
            reference = {'file': filename[:128], 'line': line,
                         'archiveEntryOccurrence': metadata.get('archiveEntryOccurrence')}
            record = {'phase': phase, 'hresult': hr, 'witness': reference}
            if kind == 'reply':
                reply_tick = row.get('replyTick')
                record['elapsedMs'] = reply_tick-tick if uint(reply_tick, 64, tick) else None
                current = row.get('currentSession')
                record['currentSession'] = current if type(current) is bool else None
                parse = row.get('parse')
                record['parse'] = parse if parse in ('parsed', 'oversize', 'unavailable') else None
                payload = row.get('renderer')
                record['rendererObjectReported'] = isinstance(payload, dict)
                if isinstance(payload, dict):
                    ready, visibility = payload.get('readyState'), payload.get('visibility')
                    record['readyState'] = ready if ready in ('loading', 'interactive', 'complete') else None
                    record['visibility'] = visibility if visibility in ('visible', 'hidden', 'prerender') else None
                    for field in ('hasFocus', 'app', 'body'):
                        item = payload.get(field)
                        record[field] = item if type(item) is bool else None
            entry[kind] = record

    states = collections.Counter()
    examples, first_by_state = [], {}
    for (pid, birth, session, tick), entry in entries.items():
        request, reply = entry['request'], entry['reply']
        dispatch_failed = request is not None and request['hresult'] is not None and request['hresult'] < 0
        if entry['requestCount'] > 1 or entry['replyCount'] > 1 or (dispatch_failed and reply is not None):
            state = 'ambiguous-or-conflicting'
        elif dispatch_failed:
            state = 'dispatch-rejected'
        elif reply is None or request is None:
            state = 'request-only' if reply is None else 'reply-only'
        elif request['hresult'] is None or reply['hresult'] is None:
            state = 'paired-status-unavailable'
        elif reply['phase'] == 'renderer-reply-invalid':
            state = 'paired-invalid-reply'
        elif reply['hresult'] < 0:
            state = 'paired-reply-error'
        elif reply['currentSession'] is False:
            state = 'paired-no-longer-current'
        elif (reply['currentSession'] is not True or reply['elapsedMs'] is None or
              reply['parse'] != 'parsed' or not reply['rendererObjectReported']):
            state = 'paired-payload-or-context-unavailable'
        else:
            state = 'paired-current-renderer-object'
        states[state] += 1
        example = {'state': state, 'writerPid': pid, 'writerProcessCreation100ns': birth,
                   'session': session, 'requestedTick': tick, **entry}
        first_by_state.setdefault(state, example)
        if len(examples) < max_examples:
            examples.append(example)
    selected = list(first_by_state.values())
    selected_ids = {id(item) for item in selected}
    selected.extend(item for item in examples if id(item) not in selected_ids)
    return {'schemaVersion': 1, 'scope': 'retained-native-writer-session-and-request-tick;not-input-or-pixel-proof',
            'rowsByReportedPhaseAndCoverage': dict(counts), 'keysByState': dict(states),
            'maxKeys': max_keys, 'maxExamples': max_examples, 'examples': selected[:max_examples],
            'absenceProvesFailure': False, 'dispatchOrScriptReplyProvesInputOrPixels': False,
            'joinsAcrossArchivesOrByTimeProximity': False}


def overlay_surface_session_evidence(members):
    """Group retained observations by exact reported identity, without ordering phases."""
    stages = frozenset(UI_SURFACE_PHASES)
    included_phases = stages | {'renderer-requested', 'renderer-replied', 'renderer-reply-invalid'}
    max_keys, max_examples = 512, 48
    entries, coverage, row_counts = {}, collections.Counter(), collections.Counter()

    def uint(value, bits=64, minimum=1):
        return type(value) is int and minimum <= value < (1 << bits)

    for filename, rows, metadata in members:
        if not re.fullmatch(r'native-(?:lifecycle|detail)\.log(?:\.(?:prev|previous|[0-9]+))?', filename):
            continue
        for line, row in rows:
            phase = row.get('phase')
            if row.get('event') != 'game-overlay-surface' or not isinstance(phase, str) or phase not in included_phases:
                continue
            row_counts[phase] += 1
            writer, birth, power = (row.get(k) for k in ('pid', 'writerProcessCreation100ns', 'powerGeneration'))
            if (not uint(writer, 32) or not isinstance(birth, str) or
                    not re.fullmatch(r'[1-9][0-9]{0,19}', birth) or int(birth) >= 1 << 64 or not uint(power)):
                coverage['writer-or-power-unavailable'] += 1
                continue
            if phase in stages:
                presentation = row.get('presentation')
                overlay = presentation.get('overlay') if isinstance(presentation, dict) else None
                session = overlay.get('session') if isinstance(overlay, dict) else None
                binding_power = overlay.get('power') if isinstance(overlay, dict) else None
                if not uint(session) or not uint(binding_power):
                    coverage['binding-unavailable'] += 1
                    continue
                if binding_power != power or ('session' in row and (not uint(row['session']) or row['session'] != session)):
                    coverage['conflicting-session-or-power'] += 1
                    continue
                source = 'presentation.overlay'
            else:
                session = row.get('session')
                if not uint(session):
                    coverage['probe-session-unavailable'] += 1
                    continue
                if phase == 'renderer-replied' and row.get('currentSession') is not True:
                    coverage['probe-reply-not-confirmed-current'] += 1
                    continue
                source = 'session'
            key = writer, birth, power, session
            entry = entries.get(key)
            if entry is None:
                if len(entries) >= max_keys:
                    coverage['row-beyond-session-limit'] += 1
                    continue
                entry = entries[key] = dict(writerPid=writer, writerProcessCreation100ns=birth,
                    powerGeneration=power, session=session, phases={})
            coverage['grouped'] += 1
            occurrence = metadata.get('archiveEntryOccurrence')
            witness = dict(file=filename, line=line, archiveEntryOccurrence=occurrence if uint(occurrence, minimum=0) else None,
                           sessionField=source)
            retained = entry['phases'].setdefault(phase, dict(rowCount=0, first=witness, last=witness))
            if phase == 'surface-return':
                returned = row.get('originalParentRestored')
                witness['originalParentRestored'] = returned if type(returned) is bool else None
                hr = row.get('hresult')
                witness['hresult'] = hr if type(hr) is int and -(1 << 31) <= hr < (1 << 31) else None
                if returned is False:
                    retained['parentRestoreFailureRows'] = retained.get('parentRestoreFailureRows', 0) + 1
                    retained.setdefault('firstParentRestoreFailure', witness)
            retained['rowCount'] += 1
            retained['last'] = witness

    patterns = collections.Counter()
    for entry in entries.values():
        phases = entry['phases']
        shown = 'presented' in phases
        returned = 'hidden' in phases or 'surface-return' in phases
        state = ('presentation-and-return-reports' if shown and returned else 'presentation-without-retained-return' if shown else
                 'return-without-retained-presentation' if returned else 'other-retained-phases')
        patterns[state] += 1
        entry['state'] = state
    return dict(scope='retained-reported-writer-power-session-keys-within-one-archive;unordered-observations-only',
        rowCountsByReportedPhase=dict(row_counts), rowCoverage=dict(coverage), sessionKeysIndexed=len(entries),
        keysByState=dict(patterns), examples=list(entries.values())[:max_examples], maxIndexedKeys=max_keys,
        maxExamples=max_examples, keysNotListed=max(0,len(entries)-max_examples),
        completeLifecycleOrOperationCountProven=False, rendererReplyLinkedToSpecificRequest=False,
        joinsAcrossArchivesOrByTimeProximity=False, screenOrInputVerified=False)


def ui_presentation_evidence(members,current_pid):
    head=[];tail=collections.deque(maxlen=8);witnesses={};total=omitted=0;classes=collections.Counter();signals=collections.Counter()
    for filename,rows,metadata in members:
        for line,row in rows:
            record=ui_presentation_record(row,filename)
            if record is None:continue
            total+=1;classes[record['state']]+=1
            if record['reportedSignal']:signals[record['reportedSignal']]+=1
            writer,role=process_identity(row,filename);birth=row.get('writerProcessCreation100ns')
            known=isinstance(birth,str) and 0<len(birth)<=20 and birth.isascii() and birth.isdecimal() and 0<int(birth)<(1<<64)
            relation='writerPidUnknown' if writer=='unknown' else 'currentPidUnspecified' if current_pid is None else 'currentPidMatch' if writer==str(current_pid) else 'otherPid'
            item={'file':filename[:160],'fileNameTruncated':len(filename)>160,'line':line,'encounterOrdinal':total,'writerPid':None if writer=='unknown' else writer,
                'writerIdentityField':role,'pidRelation':relation,'writerProcessCreation100ns':birth if known else None,
                'writerBirthState':'reported-decimal' if known else 'missing' if 'writerProcessCreation100ns' not in row else 'null' if birth is None else 'invalid',
                'record':record}
            for key in ('archiveEntryOccurrence','containerOnly'):
                v=metadata.get(key)
                if key=='archiveEntryOccurrence' and type(v) is int and 0<=v<(1<<64) or key=='containerOnly' and type(v) is bool:item[key]=v
            t=row.get('time')
            if isinstance(t,str) and len(t)<=40:item['reportedTime']=t
            if len(head)<8:head.append(item)
            else:tail.append(item)
            key=(record['event'],record['state'],record['reportedSignal'],relation)
            if key not in witnesses:
                if len(witnesses)<32:witnesses[key]=item
                else:omitted+=1
    listed=sorted({x['encounterOrdinal']:x for x in head+list(tail)+list(witnesses.values())}.values(),key=lambda x:x['encounterOrdinal'])
    return {'schemaVersion':1,'scope':'retained-original-surface-and-WebView-reports;not-crash-health-or-pixel-proof',
        'rowsObserved':total,'countsByState':dict(classes),'rowCountsByReportedSignal':dict(signals),
        'observations':listed,'maxListedObservations':48,'rowsNotListed':total-len(listed),
        'maxClassificationWitnesses':32,'classificationWitnessRowsNotTracked':omitted,
        'operationSessionsPaired':False,'uniqueOperationsOrCrashesCounted':False,
        'absenceProvesNoCrash':False,'currentUiOrInputHealthProven':False,'runtimeSourceOrLoadedProven':False,
        'readingRules':['browser-process-exited-handler is callback registration;registered=true and HRESULT0 are not a browser exit',
            'presented/controllerReady are producer reports;screenVisibleVerified=false stays false;no pixels or game input inferred',
            'hidden/surface-return do not prove process exit;process-failed and browser-normal-exit are separate reported events',
            'retained rows are not joined into sessions or timelines;writer PID match is not a lifetime proof;birth is same-row decimal only',
            'unknown/null/bad fields stay explicit;raw URI,description,module,title and arbitrary extra fields are not copied',
            'first8,last8,first32 typed witnesses in encounter order;no repeat-row lane violation or absence-of-crash inference']}


CALIBRATION_RESTORE_EVENTS = ('gyro-calibration-awaiting-current-source','gyro-calibration-awaiting-current-recipe','gyro-calibration-restored')


def calibration_restore_record(row):
    """Projection of existing calibration receipts;never a fresh sensor/storage read."""
    if not isinstance(row,dict) or row.get('event') not in CALIBRATION_RESTORE_EVENTS:return None
    event=row['event'];fields={};states={};issues=[];conflicts=[];requested=set()
    def take(key,kind,choices=()):
        requested.add(key)
        if key not in row:states[key]='missing';issues.append(key+':missing');return None
        value=row[key]
        if value is None:state='null'
        elif kind=='bool':state='known' if type(value) is bool else 'invalid-type-range'
        elif kind=='i32':state='known' if type(value) is int and -(1<<31)<=value<(1<<31) else 'invalid-type-range'
        elif kind=='finite':state='known' if finite_number(value) else 'invalid-type-range'
        else:state='known' if isinstance(value,str) and value in choices else 'unsupported-value' if isinstance(value,str) else 'invalid-type-range'
        if state!='known':states[key]=state;issues.append(key+':'+state);return None
        fields[key]=value;return value
    def finite_number(value):
        if type(value) not in (int,float):return False
        try:return math.isfinite(value)
        except (OverflowError,ValueError):return False
    shape=None;disposition=None
    if event=='gyro-calibration-awaiting-current-source':
        shape='source-wait';qualified=take('sourceQualified','bool');current=take('sourceCurrent','bool');take('calibrationOwner','enum',('none',))
        if qualified is False or current is True:conflicts.append('source-wait-flags-conflict')
        disposition='reported-awaiting-current-source'
    elif event=='gyro-calibration-awaiting-current-recipe' or any(k in row for k in ('matrixId','matrixSourceSha256','sourceQualified','loaded')):
        shape='recipe-result';qualified=take('sourceQualified','bool');loaded=take('loaded','bool');weight=take('weight','i32');confidence=take('confidence','finite')
        if qualified is False:conflicts.append('recipe-result-not-source-qualified')
        if event=='gyro-calibration-awaiting-current-recipe':
            if loaded is True or weight not in (None,0) or confidence not in (None,0):conflicts.append('waiting-recipe-with-loaded-result')
            disposition='reported-awaiting-stored-calibration'
        else:
            if loaded is False:conflicts.append('restored-event-with-loaded-false')
            disposition='reported-recipe-calibration-loaded'
        # Both recipe witnesses are declared provenance,never raw device IDs/paths.
        for key in ('matrixId','matrixSourceSha256'):
            requested.add(key);value=row.get(key)
            state='missing' if key not in row else 'null' if value is None else 'known' if isinstance(value,str) and 0<len(value)<=256 else 'invalid-type-range'
            states[key]=state
            if state=='known':fields[key+'Present']=True
            else:issues.append(key+':'+state)
    elif 'offset' in row:
        shape='legacy-offset-result';take('weight','i32');take('confidence','finite');requested.add('offset');offset=row['offset']
        if not isinstance(offset,dict):states['offset']='null' if offset is None else 'invalid-type-range';issues.append('offset:'+states['offset'])
        else:
            result={}
            for key in ('x','y','z'):
                value=offset.get(key)
                state='missing' if key not in offset else 'null' if value is None else 'known' if finite_number(value) else 'invalid-type-range'
                states['offset.'+key]=state
                if state=='known':result[key]=value
                else:issues.append('offset.'+key+':'+state)
            fields['offset']=result
        disposition='reported-legacy-calibration-loaded'
    else:issues.append('restored-shape:missing');shape='unqualified-restored-shape'
    state=('contradictory' if conflicts else 'invalid-fields' if any(':invalid-type-range' in q for q in issues) else
        'unsupported-values' if any(':unsupported-value' in q for q in issues) else 'null-fields' if any(':null' in q for q in issues) else
        'missing-fields' if issues else 'reported-fields')
    return {'state':state,'shape':shape,'reported':fields,'fieldStates':states,'fieldIssues':issues+conflicts,
        'reportedDisposition':disposition if state=='reported-fields' else None,
        'unprojectedFieldCount':len(set(row)-requested-TRANSPORT_FIELDS-{'event'}),
        'storedRecordPresenceIndependentlyVerified':False,'currentSensorOrInputHealthProven':False,'operationsPaired':False}


def calibration_restore_evidence(members,current_pid):
    head=[];tail=collections.deque(maxlen=8);witnesses={};total=omitted=0
    events=collections.Counter();states=collections.Counter();dispositions=collections.Counter();relations=collections.Counter()
    for filename,rows,metadata in members:
        if not re.fullmatch(r'native-(?:lifecycle|detail)(?:\.log(?:\.(?:prev|previous|[0-9]+))?|\.(?:prev|previous|[0-9]+)\.log)',filename):continue
        for line,row in rows:
            record=calibration_restore_record(row)
            if record is None:continue
            total+=1;events[row['event']]+=1;states[record['state']]+=1
            if record['reportedDisposition']:dispositions[record['reportedDisposition']]+=1
            pid,role=process_identity(row,filename)
            relation='writerPidUnknown' if pid=='unknown' else 'currentPidUnspecified' if current_pid is None else 'currentPidMatch' if pid==str(current_pid) else 'otherPid'
            relations[relation]+=1
            birth=row.get('writerProcessCreation100ns');known=isinstance(birth,str) and 0<len(birth)<=20 and birth.isascii() and birth.isdecimal() and 0<int(birth)<(1<<64)
            item={'file':filename[:160],'line':line,'encounterOrdinal':total,'event':row['event'],'writerPid':None if pid=='unknown' else pid,'writerIdentityField':role,'pidRelation':relation,
                'writerProcessCreation100ns':birth if known else None,'writerBirthState':'reported-decimal' if known else 'missing' if 'writerProcessCreation100ns' not in row else 'null' if birth is None else 'invalid',
                'calibration':record}
            occurrence=metadata.get('archiveEntryOccurrence')
            if type(occurrence) is int and 0<=occurrence<(1<<64):item['archiveEntryOccurrence']=occurrence
            if len(filename)>160:item['originalFileNameSha256']=hashlib.sha256(filename.encode('utf8')).hexdigest()
            if len(head)<8:head.append(item)
            else:tail.append(item)
            key=(row['event'],record['shape'],record['state'],record['reportedDisposition'])
            if key not in witnesses:
                if len(witnesses)<32:witnesses[key]=item
                else:omitted+=1
    listed=sorted({q['encounterOrdinal']:q for q in head+list(tail)+list(witnesses.values())}.values(),key=lambda q:q['encounterOrdinal'])
    return {'schemaVersion':1,'scope':'retained-calibration-restore-reports;not-storage-census-or-input-recovery',
        'rowsObserved':total,'countsByEvent':dict(events),'countsByState':dict(states),'countsByReportedDisposition':dict(dispositions),'rowCountsByPidRelation':dict(relations),
        'observations':listed,'maxListedObservations':48,'rowsNotListed':total-len(listed),'maxClassificationWitnesses':32,'classificationWitnessRowsNotTracked':omitted,
        'storedRecordPresenceIndependentlyVerified':False,'currentSensorOrInputHealthProven':False,'operationsPaired':False,'sourceCurrentOrLoadedProven':False,
        'readingRules':['source wait and missing stored calibration are distinct producer paths;sourceQualified is recipe admission,not physical source health',
            'restored is reported loading/application,not live sensor samples,game consumption or independently reread storage;legacy offset and recipe payloads remain distinct',
            'missing,null,type/range/finite errors,unsupported values and contradictions remain visible;absence is never healthy or zero',
            'device IDs,matrix strings,source digest strings and arbitrary extras stay in raw bytes;presence flags do not validate provenance',
            'only root Native lifecycle/detail and ASCII rotations;first8,last8,first32 typed witnesses;no adjacent-row/session join,PID-only lifetime or lane-rate inference']}


def native_finite_outcome_record(row):
    """Typed reported fields only; no API query, action, or current-health inference."""
    if not isinstance(row,dict) or row.get('event') not in FINITE_NATIVE_EVENTS:return None
    event=row['event'];fields={};states={};issues=[];requested=set();contradictions=[]
    def take(key,kind,required=True,choices=(),positive=False):
        requested.add(key)
        if key not in row:
            states[key]='missing'
            if required:issues.append((key,'missing'))
            return None
        value=row[key]
        if value is None:state='null'
        elif kind in ('u32','u64'):
            state='known' if type(value) is int and (1 if positive else 0)<=value<(1<<(32 if kind=='u32' else 64)) else 'invalid-type-range'
        elif kind=='bool':state='known' if type(value) is bool else 'invalid-type-range'
        elif kind=='id':state='known' if isinstance(value,str) and 0<len(value)<=128 and all(32<=ord(c)<=126 for c in value) else 'invalid-type-range'
        else:state='known' if isinstance(value,str) and value in choices else 'unsupported-value' if isinstance(value,str) else 'invalid-type-range'
        if state!='known':states[key]=state;issues.append((key,state));return None
        if kind=='id':fields[key+'Present']=True
        else:fields[key]=value
        return value
    disposition=None
    if event=='shortcut-action-request':
        status=take('status','enum',choices=('issued','not-issued'));take('actionId','enum',choices=FINITE_SHORTCUT_ACTIONS)
        take('id','id',required=status=='issued')
        disposition={'issued':'request-issued','not-issued':'request-not-issued'}.get(status)
    elif event=='shortcut-action-result':
        schema=take('schemaVersion','u64');take('origin','enum',choices=('controller-shortcut',));take('actionId','enum',choices=FINITE_SHORTCUT_ACTIONS);take('id','id')
        state=take('state','enum',choices=('applied','saved','pending','failed'))
        if schema is not None and schema!=1:states['schemaVersion']='unsupported-value';issues.append(('schemaVersion','unsupported-value'))
        disposition={'applied':'reported-applied','saved':'reported-saved','pending':'reported-pending','failed':'reported-failed'}.get(state)
    elif event=='msi-claw-return-debt':
        action=take('action','enum',choices=('retained','discharged'));basis=take('basis','enum',choices=('release-unproven','readback-verified','xinput-readmitted'))
        take('returnDebtEpisodeId','u64',positive=True);take('nativePid','u32',positive=True)
        for key in ('powerGeneration','producerPowerGeneration','observedPowerGeneration'):take(key,'u64',required=key=='powerGeneration')
        for key in ('connectedMask','physicalSourceMask','admittedMask','externalPrioritySourceMask'):take(key,'u32')
        for key in ('externalPrioritySuspended','routeDisarmed','boardAuthorized'):take(key,'bool')
        readback=take('readbackVerified','bool',required=action=='retained');readmitted=take('xinputReAdmitted','bool',required=action=='retained');retained=take('returnDebtRetained','bool',required=action=='retained')
        if action=='discharged':
            take('ownerToken','u64');take('hostEpoch','u64')
            if basis=='release-unproven' or retained is True:contradictions.append('discharge-with-unproven-or-retained-basis')
        elif action=='retained' and (basis not in (None,'release-unproven') or readback is True or readmitted is True or retained is False):contradictions.append('retained-with-conflicting-qualification')
        disposition={'retained':'debt-retained','discharged':'reported-debt-discharged'}.get(action)
    elif event=='steam-overlay-startup':
        take('ok','bool');take('scope','enum',choices=('client-setting',));game=take('gameOverlayVerified','bool')
        for key in ('pending','liveConfirmed','persisted','fileFallback','safeFileRetry'):take(key,'bool',required=False)
        if game is True:contradictions.append('client-setting-receipt-cannot-prove-game-overlay')
        disposition='startup-setting-observation'
    elif event=='process-efficiency-startup':
        for key in ('ownedTasksFound','ownedTasksConfirmed','ownedTasksPartial'):take(key,'u64')
        for key in ('selfPriority','selfEcoQoSError','selfPriorityError'):take(key,'u32')
        for key in ('selfEcoQoS','selfPriorityConfirmed'):take(key,'bool')
        take('policy','enum',choices=('ecoqos-idle-v1',));disposition='startup-policy-observation'
    elif event=='input-module-replay-unverified':
        created=take('newHostCreated','bool');stopped=take('automaticRetryStopped','bool')
        if created is True or stopped is False:contradictions.append('unverified-replay-receipt-flags-conflict')
        disposition='unverified-replay-stopped'
    elif event=='hwinfo-recovery-launch':
        queued=take('result','enum',choices=('queued',))
        confirmed=take('startupConfirmed','bool')
        take('trigger','enum',choices=('monitor-hw-unhealthy-armed',))
        take('reason','enum',choices=('persistent-producer-stall','shared-memory-unavailable'))
        take('stallGraceMs','u64',required=False)
        take('owner','enum',choices=('ymcc-native',))
        poll=take('producerPollTime','u64',required=False)
        if 'producerAgeMs' in row and row['producerAgeMs'] is None:
            requested.add('producerAgeMs');states['producerAgeMs']='not-observed'
            if 'producerPollTime' in row:fields['producerAgeMs']=None
            else:issues.append(('producerAgeMs','null'))
        else:
            age=take('producerAgeMs','u64')
            if age is not None and 'producerPollTime' not in row:
                fields['legacyReportedProducerAgeMs']=fields.pop('producerAgeMs')
                states['producerAgeMs']='legacy-unqualified'
            elif age is not None and (poll is None or poll==0 or poll>((1<<63)-1)//1000):
                fields.pop('producerAgeMs')
                states['producerAgeMs']='unqualified'
                contradictions.append('producer-age-without-usable-poll-time')
        if confirmed is True:contradictions.append('queued-launch-cannot-confirm-startup')
        if queued=='queued' and confirmed is False:disposition='recovery-launch-queued'
    elif event=='hwinfo-recovery-result-discarded':
        take('code','u32')
        for key in ('completedGeneration','currentGeneration','completedDemandGeneration','demandGeneration'):take(key,'u64')
        take('powerReady','bool');take('owner','enum',choices=('ymcc-native',));disposition='recovery-result-discarded'
    elif event=='gyro-power-early-pause-completed':
        take('source','enum',choices=('os-suspend-callback',));stopped=take('subscriptionsStopped','bool')
        take('generation','u64');physical=take('physicalSleepVerified','bool')
        if physical is True:contradictions.append('sensor-callback-receipt-does-not-verify-physical-sleep')
        disposition='reported-subscriptions-stopped' if stopped is True else 'reported-subscriptions-retained' if stopped is False else None
    elif event=='gyro-power-start-current':
        completed=take('commandCompleted','bool');take('publicPowerCurrent','bool');take('suspendRequested','bool')
        for key in ('powerGeneration','producerPowerGeneration','observedPowerGeneration','notificationSequence','suspendSerial'):take(key,'u64',required=key not in ('producerPowerGeneration','observedPowerGeneration'))
        physical=take('physicalSleepVerified','bool')
        if physical is True:contradictions.append('sensor-start-receipt-does-not-verify-physical-sleep')
        disposition='reported-start-command-completed' if completed is True else 'reported-start-command-not-completed' if completed is False else None
    elif event=='fxsound-toggle':
        accepted=take('ok','bool')
        for key in ('enabled','copied','reused','repaired'):take(key,'bool')
        disposition='reported-toggle-accepted' if accepted is True else 'reported-toggle-refused' if accepted is False else None
    elif event=='settings-upgrade-repaired':
        take('status','enum',choices=('repaired',));take('source','enum',choices=('defaults-size-limit','primary','backup','defaults'))
        take('archived','u32');take('automaticHardwareDeactivated','bool')
        disposition='reported-settings-repaired'
    elif event=='gamepad-feedback-stop-pending':
        phase=take('phase','enum',choices=('bounded-retry','awaiting-identity-event','settled'))
        retained=take('ownershipRetained','bool');accepted=take('zeroApiAccepted','bool');physical=take('hardwareStopObserved','bool')
        take('slot','u32');take('pulseGeneration','u64',positive=True);take('identityEpisode','u64')
        pending=phase in ('bounded-retry','awaiting-identity-event')
        if pending:
            delay=take('retryInMs','u32')
            for key in ('identityEpoch','sourceGeneration','sourcePowerGeneration'):take(key,'u64')
            take('legacyPathless','bool')
            reason=take('reason','enum',choices=('zero-write-failed','identity-unresolved'))
            requested.add('writeError')
            if 'writeError' not in row:states['writeError']='missing';issues.append(('writeError','missing'));error=None
            elif row['writeError'] is None:fields['writeError']=None;states['writeError']='not-reported';error=None
            else:error=take('writeError','u32')
            if retained is False or accepted is True:contradictions.append('pending-stop-with-settled-flags')
            if phase=='bounded-retry' and delay==0 or phase=='awaiting-identity-event' and delay not in (None,0):contradictions.append('stop-phase-and-retry-delay-conflict')
            if reason=='zero-write-failed' and error in (None,0) or reason=='identity-unresolved' and error is not None:contradictions.append('stop-reason-and-error-observation-conflict')
        elif phase=='settled':
            reason=take('reason','enum',choices=('zero-api-accepted','source-retired-or-power-gated'))
            if retained is True or reason=='zero-api-accepted' and accepted is False or reason=='source-retired-or-power-gated' and accepted is True:contradictions.append('settled-stop-with-conflicting-api-flags')
        if physical is True:contradictions.append('feedback-stop-receipt-does-not-verify-hardware-stop')
        disposition={'bounded-retry':'reported-stop-retry-pending','awaiting-identity-event':'reported-stop-awaiting-identity','settled':'reported-stop-settled'}.get(phase)
    else:
        subscribed=take('subscribed','bool');error=take('error','u32');take('captureEpoch','u64')
        if subscribed is True and error not in (None,0):contradictions.append('subscribed-with-nonzero-registration-error')
        disposition='registration-reported-subscribed' if subscribed is True else 'registration-reported-failed' if subscribed is False else None
    kinds={kind for _,kind in issues}
    state=('contradictory' if contradictions else 'invalid-fields' if 'invalid-type-range' in kinds else
        'unsupported-values' if 'unsupported-value' in kinds else 'null-fields' if 'null' in kinds else
        'missing-fields' if 'missing' in kinds else 'reported-fields')
    return {'state':state,'reported':fields,'fieldStates':states,
        'fieldIssues':[key+':'+kind for key,kind in issues]+contradictions,
        'reportedDisposition':disposition if state=='reported-fields' else None,
        'unprojectedFieldCount':len(set(row)-requested-TRANSPORT_FIELDS-{'event'}),
        'currentBusinessSuccessInferred':False,'physicalOrResourceReturnProven':False}


def native_finite_outcome_evidence(members,current_pid):
    head=[];tail=collections.deque(maxlen=8);witnesses={};total=untracked=0
    events=collections.Counter();states=collections.Counter();dispositions=collections.Counter()
    relations=collections.Counter();births=collections.Counter()
    for filename,rows,metadata in members:
        if not re.fullmatch(r'native-(?:lifecycle|detail)(?:\.log(?:\.(?:prev|previous|[0-9]+))?|\.(?:prev|previous|[0-9]+)\.log)',filename):continue
        for line,row in rows:
            detail=native_finite_outcome_record(row)
            if detail is None:continue
            total+=1;events[row['event']]+=1;states[detail['state']]+=1
            if detail['reportedDisposition'] is not None:dispositions[detail['reportedDisposition']]+=1
            writer,role=process_identity(row,filename)
            relation='writerPidUnknown' if writer=='unknown' else 'currentPidUnspecified' if current_pid is None else 'currentPidMatch' if writer==str(current_pid) else 'otherPid'
            relations[relation]+=1
            birth=row.get('writerProcessCreation100ns');known=isinstance(birth,str) and 0<len(birth)<=20 and birth.isascii() and birth.isdecimal() and 0<int(birth)<(1<<64)
            birth_state='missing' if 'writerProcessCreation100ns' not in row else 'null' if birth is None else 'reported-decimal' if known else 'invalid-type' if not isinstance(birth,str) else 'invalid-value';births[birth_state]+=1
            ref={'file':filename[:160],'line':line,'encounterOrdinal':total,'event':row['event'],
                'writerPid':None if writer=='unknown' else writer,'writerIdentityField':role,'pidRelation':relation,
                'writerProcessCreation100ns':birth if known else None,'writerBirthState':birth_state,'outcome':detail}
            if len(filename)>160:ref['originalFileNameSha256']=hashlib.sha256(filename.encode('utf8')).hexdigest()
            for key in ('archiveEntryOccurrence','containerOnly'):
                value=metadata.get(key)
                if key=='archiveEntryOccurrence' and type(value) is int and 0<=value<(1<<64) or key=='containerOnly' and type(value) is bool:ref[key]=value
            if len(head)<8:head.append(ref)
            else:tail.append(ref)
            key=(row['event'],detail['state'],detail['reportedDisposition'])
            if key in witnesses:continue
            if len(witnesses)<64:witnesses[key]=ref
            else:untracked+=1
    selected=sorted({q['encounterOrdinal']:q for q in head+list(tail)+list(witnesses.values())}.values(),key=lambda q:q['encounterOrdinal'])
    return {'schemaVersion':1,'scope':'retained-Native-finite-outcome-reports;not-current-business-or-resource-proof',
        'rowsObserved':total,'countsByEvent':dict(events),'countsByState':dict(states),'countsByReportedDisposition':dict(dispositions),
        'rowCountsByPidRelation':dict(relations),'rowCountsByWriterBirthState':dict(births),
        'observations':selected,'maxListedObservations':80,'rowsNotListed':total-len(selected),
        'maxClassificationWitnesses':64,'classificationWitnessRowsNotTracked':untracked,
        'selection':'first8,last8,first64 event/classification/disposition witnesses;encounter order,not chronology',
        'uniqueOperationsOrFailuresCounted':False,'repeatedRowsProveLaneViolation':False,
        'currentBusinessSuccessInferred':False,'physicalOrResourceReturnProven':False,'sourceCurrentOrLoadedInferred':False,
        'readingRules':['issued is a request only;pending is unresolved;applied/saved/failed are reported shortcut feedback,not independently verified OS or hardware actions',
            'retained debt is unresolved;discharged is a reported qualified settlement,not current resource census or proof of physical return;never infer the latest state across files',
            'discarded recovery code0 remains a discarded completion;startup policy partial counts and false flags are preserved;registration success is not input consumption',
            'sensor command completion,public power current and suspend request remain independent;subscriptionsStopped is reported,not an OS census or physical sleep proof',
            'feedback stop retry,dormant ownership and settlement remain separate;zero API accepted is not hardware stop;settings deactivation is reported configuration only',
            'missing,null,bad types,unsupported values and contradictions remain distinct;raw titles/messages/paths/accounts and extra fields are not copied',
            'Native root lifecycle/detail members and ASCII rotations only;duplicate occurrences remain separate rows;PID match alone never proves a writer lifetime or current source']}


def native_http_failure_record(row):
    """Reported log metadata,not an independent API query or failure cause."""
    value=row.get('httpFailure')
    state='missing' if 'httpFailure' not in row else 'null' if value is None else 'object' if isinstance(value,dict) else 'invalid-type'
    result={'state':state,'reported':{},'fieldIssues':[],'unprojectedFieldCount':0,
        'reportedKind':None,'codeIsIndependentApiObservation':False,'deadlineFlagDeterminesKind':False}
    if state!='object':return result
    scope='exact-existing-error-string;not-independent-api-observation'
    if type(value.get('schema')) is not int or value['schema']!=1 or value.get('scope')!=scope:
        result['state']='unsupported';return result
    phases={phase for values in HTTP_FAILURE_PHASES.values() for phase in values}
    specs=(('schema',lambda v:type(v) is int and v==1),
        ('kind',lambda v:isinstance(v,str) and v in HTTP_FAILURE_PHASES),
        ('phase',lambda v:v is None or isinstance(v,str) and v in phases),
        ('api',lambda v:v is None or isinstance(v,str) and v in HTTP_SETUP_APIS.values()),
        ('codeKnown',lambda v:type(v) is bool),
        ('codeDomain',lambda v:v is None or v=='winhttp-error-from-reported-text'),
        ('code',lambda v:v is None or type(v) is int and 0<=v<(1<<32)),
        ('scope',lambda v:v==scope))
    fields=result['reported'];issues=result['fieldIssues']
    for key,valid in specs:
        if key not in value:issues.append(key+':missing')
        elif not valid(value[key]):issues.append(key+':invalid-type-range-or-value')
        else:fields[key]=value[key]
    result['unprojectedFieldCount']=len(value)-sum(key in value for key,_ in specs)
    if issues:result['state']='invalid-fields';return result
    kind=fields['kind'];phase=fields['phase'];api=fields['api']
    if phase not in HTTP_FAILURE_PHASES[kind]:issues.append('kind:phase-conflict')
    if kind=='transport-setup-failed':
        if api!=HTTP_SETUP_APIS.get(phase):issues.append('setup:api-phase-conflict')
    elif api is not None:issues.append('non-setup:unexpected-api')
    if kind=='winhttp-failed':
        if fields['codeKnown'] is not True or fields['codeDomain']!='winhttp-error-from-reported-text' or fields['code'] is None:
            issues.append('winhttp:code-missing-or-conflicting')
    elif fields['codeKnown'] is not False or fields['codeDomain'] is not None or fields['code'] is not None:
        issues.append('non-winhttp:unexpected-code')
    result['state']='contradictory' if issues else 'reported-unknown' if kind=='unknown' else 'reported-classified'
    if not issues:result['reportedKind']=kind
    return result


def native_http_failure_evidence(members,current_pid):
    head,tail,witnesses=[],collections.deque(maxlen=8),{}
    states=collections.Counter();kinds=collections.Counter();total=0
    identity_counts={'currentPidMatch':0,'otherPid':0,'writerPidUnknown':0,'currentPidUnspecified':0}
    birth_states=('missing','null','reported-decimal','invalid-type','invalid-value')
    birth_counts={key:0 for key in birth_states}
    birth_by_pid={relation:{key:0 for key in birth_states} for relation in identity_counts}
    reference_birth=None;birth_witnesses=[];same_birth_rows=other_birth_rows=0
    def writer_birth(row):
        if 'writerProcessCreation100ns' not in row:return 'missing',None
        value=row['writerProcessCreation100ns']
        if value is None:return 'null',None
        if not isinstance(value,str):return 'invalid-type',None
        known=0<len(value)<=20 and value.isascii() and value.isdecimal() and 0<int(value)<(1<<64)
        return ('reported-decimal',value) if known else ('invalid-value',None)
    for filename,rows,metadata in members:
        if not re.fullmatch(r'native-(?:lifecycle|detail)(?:\.log(?:\.(?:prev|previous|\d+))?|\.(?:prev|previous|\d+)\.log)',filename):continue
        for line,row in rows:
            if row.get('event')!='http-request-failed':continue
            total+=1;detail=native_http_failure_record(row);states[detail['state']]+=1
            writer,role=process_identity(row,filename)
            relation=('writerPidUnknown' if writer=='unknown' else 'currentPidUnspecified' if current_pid is None
                else 'currentPidMatch' if writer==str(current_pid) else 'otherPid')
            identity_counts[relation]+=1
            birth_state,birth=writer_birth(row)
            birth_counts[birth_state]+=1;birth_by_pid[relation][birth_state]+=1
            if relation=='currentPidMatch' and birth is not None:
                # Compare exact uint64 decimal values, never a JS/float number.
                # Keep only the first reference and first conflicting row.
                numeric_birth=int(birth)
                if reference_birth is None:reference_birth=numeric_birth
                same=numeric_birth==reference_birth
                if same:same_birth_rows+=1
                else:other_birth_rows+=1
                if not birth_witnesses or (not same and len(birth_witnesses)==1):
                    witness={'file':filename,'line':line,'encounterOrdinal':total,
                        'writerPid':writer,'writerIdentityField':role,'writerProcessCreation100ns':birth,
                        'reason':'first-reported-birth' if same else 'first-conflicting-reported-birth'}
                    for key in ('archiveEntryOccurrence','containerOnly'):
                        if key in metadata:witness[key]=metadata[key]
                    birth_witnesses.append(witness)
            if detail['reportedKind'] is not None:kinds[detail['reportedKind']]+=1
            ref=(total,filename,line,row,metadata,detail)
            if len(head)<8:head.append(ref)
            else:tail.append(ref)
            witnesses.setdefault(('state',detail['state']),ref)
            if detail['reportedKind'] is not None:witnesses.setdefault(('kind',detail['reportedKind']),ref)
    selected=sorted({ref[0]:ref for ref in head+list(tail)+list(witnesses.values())}.values(),key=lambda ref:ref[0])
    observations=[]
    for ordinal,filename,line,row,metadata,detail in selected:
        fields={};field_states={}
        for key in ('queueWaitMs','executionMs','timeoutMs','deadlineExpired'):
            if key not in row:field_states[key]='missing';continue
            value=row[key]
            if value is None:fields[key]=None;field_states[key]='null';continue
            valid=type(value) is bool if key=='deadlineExpired' else type(value) is int and 0<=value<(1<<64)
            field_states[key]='recorded' if valid else 'invalid-type-or-range'
            if valid:fields[key]=value
        writer,role=process_identity(row,filename);birth_state,birth=writer_birth(row)
        time=stamp(row,filename)
        observations.append({'file':filename,'line':line,**metadata,'encounterOrdinal':ordinal,'event':'http-request-failed',
            'writerPid':None if writer=='unknown' else writer,'writerIdentityField':role,
            'writerProcessCreation100ns':birth,'writerBirthState':birth_state,'writerLifetimeProven':False,
            'currentPidMatch':None if writer=='unknown' or current_pid is None else writer==str(current_pid),
            'time':time if time is None or len(time)<=512 and not any(0xD800<=ord(c)<=0xDFFF for c in time) else None,
            'timingAndLateDeadlineObservation':{'reported':fields,'fieldStates':field_states},'httpFailure':detail})
    birth_consistency=('current-pid-unspecified' if current_pid is None else
        'no-pid-matched-rows' if not identity_counts['currentPidMatch'] else
        'no-known-reported-birth' if not birth_witnesses else
        'conflicting-reported-births-observed' if other_birth_rows else 'one-reported-birth-observed')
    return {'schemaVersion':1,'scope':'retained-Native-HTTP-failure-log-metadata;not-transport-or-unique-operation-proof',
        'failureRowsObserved':total,'rowCountsByPidRelation':identity_counts,'countsByClassificationState':dict(states),'countsByReportedKind':dict(kinds),
        'reportedWriterBirthEvidence':{'schemaVersion':1,'scope':'same-row-reported-birth-quality-and-PID-match-consistency;not-current-lifetime-proof',
            'rowCountsByBirthState':birth_counts,'rowCountsByPidRelationAndBirthState':birth_by_pid,
            'pidMatchedBirthConsistency':birth_consistency,'pidMatchedKnownBirthRows':same_birth_rows+other_birth_rows,
            'pidMatchedRowsWithFirstReportedBirth':same_birth_rows,'pidMatchedRowsWithOtherReportedBirths':other_birth_rows,
            'pidMatchedDistinctBirthsLowerBound':len(birth_witnesses),'maxListedBirthWitnesses':2,'birthWitnesses':birth_witnesses,
            'firstReportedBirthIsCurrentAuthority':False,'allPidMatchedRowsHaveKnownBirth':bool(identity_counts['currentPidMatch']) and
                same_birth_rows+other_birth_rows==identity_counts['currentPidMatch'],
            'writerLifetimeProven':False,'manifestOrNeighborSuppliesMissingBirth':False},
        'observations':observations,'maxListedObservations':34,'rowsNotListed':total-len(observations),
        'selection':'first8,last8,first-of-eight-fixed-states-and-eleven-fixed-kinds;file-encounter-order',
        'uniqueOperationsOrRetriesCounted':False,'deadlineFlagSuppliesFailureKind':False,'independentApiCodeProven':False,
        'sourceCurrentOrLoadedInferred':False,'resourceReturnOrDeviceResultProven':False,
        'readingRules':['legacy missing,null,unsupported,bad-field,contradictory and reported unknown categories stay distinct',
            'classification is the original producer interpretation of an exact existing error string,not an independent API query',
            'deadlineExpired is a separate later observation;timing/timeout equality is not request identity or a retry count',
            'PID relation counts cover all qualifying rows,not only listed samples;PID match is not writer lifetime or operation identity',
            'birth quality counts cover all qualifying rows;missing/null/malformed stay unknown without manifest or neighboring-row backfill',
            'PID-matched birth witnesses retain only first reported birth and first conflict;distinct count is a lower bound capped at2,not an exact lifetime census',
            'one reported birth does not prove the selected current lifetime;leading-zero decimal aliases compare by exact integer value while original strings remain unchanged',
            'reported cancellation cannot uniquely identify shutdown versus a retired power grant;unknown old rows are never decoded from neighboring rows',
            'only root Native lifecycle/detail members and exact event names qualify;rotations and duplicate ZIP occurrences remain separate',
            'fixed fields are expanded only for selected rows;URL,body,raw error text and arbitrary unknown metadata are not copied']}


SENSOR_RETIREMENT_EVENTS={'gyro-power-unsubscribe-failed':'unsubscribe','gyro-power-interval-reset-failed':'interval-reset'}
SENSOR_RETIREMENT_KINDS=('winrt-hresult','allocation-exception','standard-exception','unknown-exception')
SENSOR_RETIREMENT_DOMAIN='HRESULT-from-caught-WinRT-exception'


def native_sensor_retirement_record(row):
    """Interpret exact reported exception fields,never reconstruct missing codes."""
    sensor=row.get('sensor')
    sensor_state=('missing' if 'sensor' not in row else 'null' if sensor is None else
        'known' if isinstance(sensor,str) and sensor in ('gyro','accel') else
        'unsupported-name' if isinstance(sensor,str) else 'invalid-type')
    value=row.get('failure')
    state='missing' if 'failure' not in row else 'null' if value is None else 'object' if isinstance(value,dict) else 'invalid-type'
    result={'state':state,'sensorState':sensor_state,'sensor':sensor if sensor_state=='known' else None,
        'reported':{},'reportedKind':None,'fieldIssues':[],'unprojectedFieldCount':0,
        'independentApiCodeObservation':False,'resourceReturnProven':False}
    if state!='object':return result
    fields=('schema','kind','codeKnown','codeDomain','code')
    result['unprojectedFieldCount']=len(value)-sum(key in value for key in fields)
    if type(value.get('schema')) is not int or value['schema']!=1:
        result['state']='unsupported';return result
    specs=(('schema',lambda v:type(v) is int and v==1),
        ('kind',lambda v:isinstance(v,str) and v in SENSOR_RETIREMENT_KINDS),
        ('codeKnown',lambda v:type(v) is bool),
        ('codeDomain',lambda v:v is None or isinstance(v,str) and v==SENSOR_RETIREMENT_DOMAIN),
        ('code',lambda v:v is None or type(v) is int and 0<=v<(1<<32)))
    reported=result['reported'];issues=result['fieldIssues']
    for key,valid in specs:
        if key not in value:issues.append(key+':missing')
        elif not valid(value[key]):issues.append(key+':invalid-type-range-or-value')
        else:reported[key]=value[key]
    if issues:result['state']='invalid-fields';return result
    if result['unprojectedFieldCount']:
        result['state']='unsupported-shape';return result
    kind=reported['kind']
    if kind=='winrt-hresult':
        if reported['codeKnown'] is not True or reported['codeDomain']!=SENSOR_RETIREMENT_DOMAIN or reported['code'] is None:
            issues.append('winrt:code-missing-or-conflicting')
    elif reported['codeKnown'] is not False or reported['codeDomain'] is not None or reported['code'] is not None:
        issues.append('non-winrt:unexpected-code')
    result['state']='contradictory' if issues else 'reported-unknown' if kind=='unknown-exception' else 'reported-classified'
    if not issues:result['reportedKind']=kind
    return result


def native_sensor_retirement_evidence(members,current_pid):
    head,tail,witnesses=[],collections.deque(maxlen=8),{}
    types={};states=collections.Counter();kinds=collections.Counter();total=eligible=untracked=0
    slot_counts={operation+':'+sensor:0 for operation in SENSOR_RETIREMENT_EVENTS.values() for sensor in ('gyro','accel')}
    slot_counts['unclassified']=0
    pid_counts={'currentPidMatch':0,'otherPid':0,'writerPidUnknown':0,'currentPidUnspecified':0}
    for filename,rows,metadata in members:
        if not re.fullmatch(r'native-(?:lifecycle|detail)(?:\.log(?:\.(?:prev|previous|[0-9]+))?|\.(?:prev|previous|[0-9]+)\.log)',filename):continue
        for line,row in rows:
            event=row.get('event')
            operation=SENSOR_RETIREMENT_EVENTS.get(event) if isinstance(event,str) else None
            if operation is None:continue
            total+=1;detail=native_sensor_retirement_record(row);states[detail['state']]+=1
            slot=operation+':'+detail['sensor'] if detail['sensor'] is not None else 'unclassified'
            slot_counts[slot]+=1
            writer,_=process_identity(row,filename)
            relation=('writerPidUnknown' if writer=='unknown' else 'currentPidUnspecified' if current_pid is None
                else 'currentPidMatch' if writer==str(current_pid) else 'otherPid')
            pid_counts[relation]+=1
            kind=detail['reportedKind']
            if kind is not None:kinds[kind]+=1
            ref=(total,filename,line,row,metadata,detail,operation,slot)
            if len(head)<8:head.append(ref)
            else:tail.append(ref)
            witnesses.setdefault(('state',detail['state']),ref)
            witnesses.setdefault(('slot',slot),ref)
            if kind is not None:witnesses.setdefault(('kind',kind),ref)
            if kind is not None and slot!='unclassified':
                eligible+=1;signature=(slot,kind,detail['reported']['code'])
                if signature in types:
                    types[signature]['rows']+=1;types[signature]['lastOrdinal']=total
                elif len(types)<16:
                    types[signature]={'index':len(types),'rows':1,'first':ref,'firstOrdinal':total,'lastOrdinal':total}
                else:untracked+=1
    selected=sorted({ref[0]:ref for ref in head+list(tail)+list(witnesses.values())+[value['first'] for value in types.values()]}.values(),key=lambda ref:ref[0])
    observations=[]
    for ordinal,filename,line,row,metadata,detail,operation,slot in selected:
        writer,role=process_identity(row,filename);birth=row.get('writerProcessCreation100ns')
        known_birth=isinstance(birth,str) and 0<len(birth)<=20 and birth.isascii() and birth.isdecimal() and 0<int(birth)<(1<<64)
        name=filename[:160];name_info={'originalCharacters':len(filename),'displayChanged':len(filename)>160}
        if len(filename)>160:name_info['originalNameSha256']=hashlib.sha256(filename.encode('utf8')).hexdigest()
        stamp_value=stamp(row,filename)
        item={'file':name,'fileLabel':name_info,'line':line,'encounterOrdinal':ordinal,'event':row['event'],
            'operation':operation,'slot':slot,'writerPid':None if writer=='unknown' else writer,'writerIdentityField':role,
            'writerProcessCreation100ns':birth if known_birth else None,'writerLifetimeProven':False,
            'currentPidMatch':None if writer=='unknown' or current_pid is None else writer==str(current_pid),
            'time':stamp_value if stamp_value is None or len(stamp_value)<=512 and not any(0xD800<=ord(c)<=0xDFFF for c in stamp_value) else None,
            'failure':detail}
        for key in ('archiveEntryOccurrence','containerOnly'):
            value=metadata.get(key)
            if (key=='archiveEntryOccurrence' and type(value) is int and 0<=value<(1<<64)) or (key=='containerOnly' and type(value) is bool):item[key]=value
        observations.append(item)
    descriptors=[{'typeIndex':value['index'],'slot':signature[0],'kind':signature[1],'code':signature[2],
        'rowsForTrackedType':value['rows'],'firstEncounterOrdinal':value['firstOrdinal'],'lastEncounterOrdinal':value['lastOrdinal']}
        for signature,value in types.items()]
    return {'schemaVersion':1,'scope':'retained-Native-sensor-retirement-exception-reports;not-resource-or-physical-proof',
        'failureRowsObserved':total,'rowCountsByPidRelation':pid_counts,'rowCountsByOperationSensor':slot_counts,
        'countsByClassificationState':dict(states),'countsByReportedKind':dict(kinds),
        'trackedErrorTypes':descriptors,'maxTrackedErrorTypes':16,'typeEligibleRows':eligible,'typeBudgetRowsNotTracked':untracked,
        'rowsWithoutTrackedType':total-sum(value['rows'] for value in types.values()),
        'observations':observations,'maxListedObservations':49,'rowsNotListed':total-len(observations),
        'selection':'first8,last8,first-of-nine-classification-states,five-slots,four-kinds,and-first16-error-types;encounter-order',
        'uniqueFailuresOrOperationsCounted':False,'untrackedRowsAreUniqueTypes':False,'frequencyRateComputed':False,
        'repeatedRowsProveLaneViolation':False,'exceptionCodeIsIndependentApiQuery':False,
        'resourceReturnOrPhysicalRecoveryProven':False,'sourceCurrentOrLoadedInferred':False,
        'readingRules':['legacy missing,null,unsupported,malformed,contradictory and explicit unknown exception remain separate',
            'only coherent schema1 winrt-hresult reports a uint32 caught-exception code;other kinds retain unknown code;do not translate HRESULT into Win32 or fabricate an API query',
            'types describe reported operation/sensor/kind/code,not writer lifetimes or unique failed API calls;PID relation is reported PID only',
            'same error rows may follow an unlogged successful API return or file recovery;row volume alone proves neither high Hz nor a duplicate violation',
            'counts cover all qualifying retained rows;type budget omissions count rows,not exact distinct types;selected rows do not certify full history',
            'root Native lifecycle/detail members and ASCII rotations qualify;duplicate archive occurrences remain individual retained rows',
            'unknown sensor names,raw exception text,URL/body,extra failure fields and arbitrary row fields are not copied into this view',
            'subscription flags,provider stop,thread/COM return and physical sensor recovery require their own original evidence']}


IPC_FAILURE_EVENTS = ('ipc-response-delivery-failed', 'ipc-response-delivery-exception', 'ipc-response-serialization-failed')
IPC_FAILURE_SELECTIONS = ('first-type', 'repeat-type', 'capacity-first-witness', 'capacity-detail', 'unclassified-first-witness', 'unclassified-detail')
IPC_FAILURE_RECOVERY_SELECTIONS = ('type-recovery-witness', 'capacity-recovery-witness', 'unclassified-recovery-witness')


def native_ipc_failure_record(row):
    """Project same-row message and selection reports;never infer command success."""
    message = {'state':'missing', 'reported':{}, 'fieldIssues':[]}
    fields = ('messageKind', 'requestIdPresent', 'requestId', 'eventNamePresent', 'eventNameOmitted', 'eventName')
    kinds = ('event', 'response', 'response-with-event', 'unclassified')
    if any(key in row for key in fields):
        issues=message['fieldIssues'];reported=message['reported']
        for key in fields:
            if key not in row:issues.append(key+':missing');continue
            value=row[key]
            valid=(isinstance(value,str) and value in kinds if key=='messageKind' else
                type(value) is bool if key in ('requestIdPresent','eventNamePresent','eventNameOmitted') else
                value is None or type(value) is int and -(1<<63)<=value<(1<<64) if key=='requestId' else
                value is None or isinstance(value,str) and len(value)<=128 and not any(0xD800<=ord(c)<=0xDFFF for c in value) and len(value.encode('utf8'))<=128)
            if not valid:issues.append(key+':invalid-type-range-or-value')
            elif key=='requestId':reported['requestIdValueState']='null' if value is None else 'reported-integer'
            else:reported[key]=value
        message['state']='invalid-fields' if issues else 'reported-'+reported['messageKind']
        if not issues:
            kind=reported['messageKind'];response=kind in ('response','response-with-event');event=kind in ('event','response-with-event')
            if (response != (reported['requestIdValueState']=='reported-integer') or
                    response and not reported['requestIdPresent'] or reported['eventNamePresent']!=event or
                    reported['eventNameOmitted'] and not event or
                    (reported['eventName'] is not None)!=(event and not reported['eventNameOmitted'])):
                message['state']='contradictory';issues.append('message-flags-disagree')
    selection={'state':'missing','reported':{},'fieldIssues':[],'laneExpectation':None}
    value=row.get('failureLogSelection')
    if 'failureLogSelection' in row:
        if value is None:selection['state']='null'
        elif not isinstance(value,dict):selection['state']='invalid-type'
        elif type(value.get('schema')) is not int or value['schema'] not in (1,2):selection['state']='unsupported'
        else:
            version=value['schema'];issues=selection['fieldIssues'];reported=selection['reported']
            keys=('schema','reason','typeIndex','scope')+(('recoveryEpoch',) if version==2 else ())
            expected=set(keys)
            reasons=IPC_FAILURE_SELECTIONS if version==1 else IPC_FAILURE_SELECTIONS+IPC_FAILURE_RECOVERY_SELECTIONS
            scope=('current-Native-process-type-witness;not-operation-or-delivery-proof' if version==1 else
                'current-Native-process-type-witness-and-reoffer;not-operation-or-delivery-proof')
            for key in keys:
                if key not in value:issues.append(key+':missing');continue
                part=value[key]
                valid=(type(part) is int and part==version if key=='schema' else
                    isinstance(part,str) and part in reasons if key=='reason' else
                    part is None or type(part) is int and 0<=part<64 if key=='typeIndex' else
                    type(part) is int and 0<=part<(1<<64) if key=='recoveryEpoch' else
                    isinstance(part,str) and part==scope)
                if valid:reported[key]=part
                else:issues.append(key+':invalid-type-range-or-value')
            selection['state']='invalid-fields' if issues else 'unsupported-shape' if set(value)!=expected else 'reported'
            if selection['state']=='reported':
                reason=reported['reason'];typed=reason in ('first-type','repeat-type','type-recovery-witness')
                if typed!=(reported['typeIndex'] is not None):
                    selection['state']='contradictory';issues.append('selection-index-disagrees')
                elif reason in IPC_FAILURE_RECOVERY_SELECTIONS and reported['recoveryEpoch']==0:
                    selection['state']='contradictory';issues.append('recovery-witness-with-zero-epoch')
                else:selection['laneExpectation']='detail-or-suppressed' if reason in ('repeat-type','capacity-detail','unclassified-detail') else 'low-type-witness'
            if version==2:selection['recoveryReofferProvesDelivery']=False
    event=row.get('event');key='hresult' if event==IPC_FAILURE_EVENTS[0] else 'jsonError' if event==IPC_FAILURE_EVENTS[2] else None
    error={'state':'not-reported-by-event-kind','codeDomain':None,'code':None}
    if key is not None:
        error['state']='missing' if key not in row else 'null' if row[key] is None else 'invalid-type-or-range'
        code=row.get(key)
        if type(code) is int and (-(1<<31)<=code<(1<<31) if key=='hresult' else 0<code<(1<<31)):
            error.update(state='reported',codeDomain='HRESULT' if key=='hresult' else 'nlohmann-json-exception-id',code=code)
    return {'message':message,'selection':selection,'error':error}


def native_ipc_failure_evidence(members,current_pid):
    head,tail,witnesses=[],collections.deque(maxlen=8),{}
    total=detail_in_low=0;messages=collections.Counter();selections=collections.Counter();errors=collections.Counter();events=collections.Counter();births=collections.Counter()
    relations={'currentPidMatch':0,'otherPid':0,'writerPidUnknown':0,'currentPidUnspecified':0}
    for filename,rows,metadata in members:
        if not re.fullmatch(r'native-(?:lifecycle|detail)(?:\.log(?:\.(?:prev|previous|[0-9]+))?|\.(?:prev|previous|[0-9]+)\.log)',filename):continue
        for line,row in rows:
            event=row.get('event')
            if not isinstance(event,str) or event not in IPC_FAILURE_EVENTS:continue
            total+=1;detail=native_ipc_failure_record(row);message=detail['message']['state'];selection=detail['selection'];error=detail['error']['state']
            selected=selection['reported']['reason'] if selection['state']=='reported' else selection['state']
            messages[message]+=1;selections[selected]+=1;errors[error]+=1;events[event]+=1
            writer,_=process_identity(row,filename)
            relation='writerPidUnknown' if writer=='unknown' else 'currentPidUnspecified' if current_pid is None else 'currentPidMatch' if writer==str(current_pid) else 'otherPid'
            relations[relation]+=1
            birth=row.get('writerProcessCreation100ns')
            birth_state=('missing' if 'writerProcessCreation100ns' not in row else 'null' if birth is None else
                'reported-decimal-string' if isinstance(birth,str) and 0<len(birth)<=20 and birth.isascii() and birth.isdecimal() and 0<int(birth)<(1<<64) else 'invalid-type-or-value')
            births[birth_state]+=1
            low=canonical_log_name(filename)=='native-lifecycle.log'
            detail_in_low+=low and selection['laneExpectation']=='detail-or-suppressed'
            ref=(total,filename,line,row,metadata,detail,birth_state)
            if len(head)<8:head.append(ref)
            else:tail.append(ref)
            for kind,value in (('message',message),('selection',selected),('event',event),('error',error)):
                witnesses.setdefault((kind,value),ref)
    selected=sorted({r[0]:r for r in head+list(tail)+list(witnesses.values())}.values(),key=lambda r:r[0])
    observations=[]
    for ordinal,filename,line,row,metadata,detail,birth_state in selected:
        writer,role=process_identity(row,filename);time=stamp(row,filename)
        item={'file':filename[:160],'line':line,'encounterOrdinal':ordinal,'event':row['event'],
            'writerPid':None if writer=='unknown' else writer,'writerIdentityField':role,
            'currentPidMatch':None if writer=='unknown' or current_pid is None else writer==str(current_pid),
            'writerProcessCreation100ns':row['writerProcessCreation100ns'] if birth_state=='reported-decimal-string' else None,
            'writerBirthState':birth_state,'writerLifetimeProven':False,
            'time':time if time is None or len(time)<=512 and not any(0xD800<=ord(c)<=0xDFFF for c in time) else None,
            'failure':detail}
        if len(filename)>160:item['originalFileNameSha256']=hashlib.sha256(filename.encode('utf8')).hexdigest()
        for key in ('archiveEntryOccurrence','containerOnly'):
            value=metadata.get(key)
            if (key=='archiveEntryOccurrence' and type(value) is int and 0<=value<(1<<64)) or (key=='containerOnly' and type(value) is bool):item[key]=value
        observations.append(item)
    return {'schemaVersion':1,'scope':'retained-Native-IPC-failure-row-reports;not-command-or-renderer-outcomes',
        'failureRowsObserved':total,'rowCountsByEvent':dict(events),'rowCountsByMessageState':dict(messages),
        'rowCountsBySelection':dict(selections),'rowCountsByErrorState':dict(errors),'rowCountsByPidRelation':relations,
        'rowCountsByReportedBirthState':dict(births),'reportedDetailSelectionInLowRows':detail_in_low,
        'observations':observations,'maxListedObservations':48,'rowsNotListed':total-len(observations),
        'selection':'first8,last8,first-of-fixed-message-selection-event-error-states;archive-encounter-order',
        'uniqueFailedCommandsCounted':False,'frequencyRateComputed':False,'errorCodeIndependentlyQueried':False,
        'typeIndexIsOperationIdentity':False,'countersReconciledToRetainedRows':False,'sourceCurrentOrLoadedInferred':False,
        'rendererReceiptOrPhysicalResultProven':False,
        'readingRules':['event names use the original ipc-response prefix even for notifications;only same-row coherent messageKind supplies the reported role',
            'legacy missing or malformed message fields remain unknown;empty command is never a failed business command',
            'selection reasons are reported Native lifetime type witnesses;first-type is not first operation,delivery or durable write',
            'schema2 recovery witnesses reuse the original type table or per-event overflow gate;epoch is an observed log rearm boundary,not a replayed message,file health or operation identity',
            'repeat-type/capacity-detail/unclassified-detail expect detail or suppression;actual retained filename is separate evidence',
            'rows and repeat counters are not unique operations,distinct types or high Hz;no cross-rotation deduplication or lifetime join',
            'HRESULT and JSON exception IDs keep different domains;delivery-exception supplies no numeric code',
            'writer birth must be a same-row uint64 decimal string;PID match or eventId never supplies missing lifetime',
            'raw command,request ID,body and arbitrary payload fields are not copied;bounded event names describe reports only']}


def frame_capacity_highest_power_evidence(value):
    result = {'state': 'invalid', 'scope': 'highest positive power generation observed at capacity; not current power or delivery proof'}
    if not isinstance(value, dict) or not _control_uint(value.get('schemaVersion'), 32):
        return result
    if value['schemaVersion'] != 1:
        return {**result, 'state': 'unsupported-schema'}
    rows = value.get('firstOutcomes')
    if 'power' not in value or not isinstance(rows, list) or len(rows) > 8:
        return result
    power = value['power']
    if power is None and not rows:
        return {**result, 'state': 'no-positive-power-rejection-observed', 'power': None, 'firstOutcomes': []}
    if not _control_uint(power) or power == 0 or not rows:
        return result
    uints = ('epoch', 'power', 'sourceContinuityGeneration', 'topologyGeneration', 'commandSequence', 'wireSampleSequence', 'wireTickMs')
    flags = ('semanticNeutral', 'nativeAccepted', 'sourceAdmitted', 'fastReceiptUsed', 'bindingTruncated')
    bindings = ('runId', 'hostInstanceId')
    seen = set()
    witnesses = []
    for row in rows:
        if (not isinstance(row, dict) or not all(_control_uint(row.get(key)) for key in uints) or
                row['power'] != power or row['epoch'] == 0 or row.get('selectionDecision') != 'capacity-rejected' or
                not all(type(row.get(key)) is bool for key in flags) or
                not all(isinstance(row.get(key), str) and len(row[key]) <= 127 for key in bindings)):
            return result
        shape = int(row['semanticNeutral']) | (int(row['nativeAccepted']) << 1) | (int(row['sourceAdmitted']) << 2)
        if (type(row.get('shape')) is not int or row['shape'] != shape or shape in seen or
                (row['bindingTruncated'] and any(row[key] for key in bindings))):
            return result
        seen.add(shape)
        witnesses.append({key: row[key] for key in ('shape', 'selectionDecision', *uints, *flags, *bindings)})
    return {**result, 'state': 'reported', 'power': power, 'firstOutcomes': witnesses}


def common_evidence_summary(availability, members, current_pid):
    """Offline observations only. Never infer native admission from renderer/Steam."""
    def obj(value):
        return value if isinstance(value, dict) else {}
    def project(value, fields):
        value=obj(value)
        return {key:value[key] for key in fields if key in value}
    def uint(value):
        return type(value) is int and 0 <= value <= (1 << 64)-1
    availability=obj(availability)
    shared=obj(availability.get('sharedPowerLifecycle'))
    native=obj(shared.get('native'))
    required=(type(native.get('known')) is bool and uint(native.get('generation')) and
              uint(native.get('phase')) and type(native.get('hardwareWritesAllowed')) is bool and
              type(native.get('notificationPending')) is bool)
    admission='unknown'
    if required:
        admitted=(native['known'] and native['generation']>0 and native['phase']==0 and
                  native['hardwareWritesAllowed'] and not native['notificationPending'])
        admission='admitted-at-export' if admitted else 'not-admitted-at-export'
    observations=CommonObservationEvidence('readiness');engine=CommonObservationEvidence('engine')
    failures=SteamFailureEvidence();fingerprints=CommonObservationEvidence('fingerprint');inspected=[];process_exports=[]
    def render_readiness(ref,row,detail,ready):
        item={**ref,'event':event_name(row),'writerPowerGeneration':row.get('writerPowerGeneration'),
              'consumerGeneration':detail.get('generation'),
              **project(detail,('phase','reason','attempts')),
              'readiness':project(ready,('operationAllowed','basis','sharedPowerLifecycle','sharedReady',
                  'finalKernelWakeAdmitted','generation','configEpoch','rejectionReason','rejectedGuard',
                  'interactiveProofAgeMs','completedProofAgeMs','gameInputVerified','continueButtonVerified'))}
        item['publicReadyDoesNotGrantConsumerAction']=ready.get('sharedReady') is True and ready.get('operationAllowed') is False
        item['proofEvidence']=consumer_proof_evidence(ready)
        item['guardReasonRecorded']=item['proofEvidence']['guardTagRecorded']
        return item

    def render_fingerprint(ref,row,receipt,contract):
        return {**ref,'event':event_name(row),
            'receipt':project(receipt,('ok','accepted','reason','mutated','scope','stage')),
            'contract':project(contract,('profile','sourceLength','sourceFnv1a32')),
            'interpretation':'method-layout-fingerprint;not-Win32-CDP-or-transport-error-code'}

    def render_engine(ref,row,detail,result):
        return {**ref,**project(row,('event','engineEvent','eventId','recoveryRoute')),
            'result':project(result,('ok','reason','route','recoveryRoute','driverEntered','publisherCalls',
                'publisherCallsKnown','consumed','nativeRecovered','inputVerified','mutationCommitAttempted','status','schema','repairAccepted','uiVerified','referenceCaptured')),
            'nativeQualification':project(result.get('failureDiagnostic'),('stage','beforeMemoryRead',
                'clientKnown','uiKnown','pairQualified','clientReason','uiReason')),
            'interpretation':'native-owner-qualification-and-receipt;not-steamFailure-transport-domain'}

    for filename, rows, metadata in members:
        inspected.append({'file':filename,**metadata,'validObjectRows':len(rows)})
        if canonical_log_name(filename) in ('virtual-adaptation-session.jsonl','model-adaptation-session.jsonl','model-adaptation-signals-session.jsonl'):
            begin=rows[0][1] if rows else {}
            end=rows[-1][1] if rows else {}
            begin_valid=begin.get('record')=='process_begin' and begin.get('schema')=='ymcc.adaptation-process-export/1'
            end_valid=end.get('record')=='process_end'
            process_exports.append({'file':filename,**metadata,
                'validObjectRows':len(rows),'beginRecognized':begin_valid,'endRecognized':end_valid,
                'inspectionScope':'valid-object-rows-only;malformed-lines-remain-in-file-quality',
                'begin':project(begin,('writerPid','writerProcessCreation100ns','snapshotTickMs','snapshotAvailable',
                    'knownFactsAtSnapshot','capacity','busyDropsAtSnapshot','capacityDropsAtSnapshot')) if begin_valid else {},
                'end':project(end,('snapshotAvailable','knownFactsAtSnapshot','writtenFacts','omittedFacts','omissions',
                    'retainedSnapshotRowsComplete','sourceFirstWitnessCoverageComplete','completeHistory')) if end_valid else {},
                'observedWitnessRows':sum(q.get('record')=='first_witness' for _,q in rows),
                'journalPersistenceInferred':False,'fullSessionCoverageProven':False})
        for line,row in rows:
            ref={'file':filename,'line':line,**metadata}
            writer,writer_field=process_identity(row,filename)
            ref.update(writerPid=None if writer=='unknown' else writer,
                       writerIdentityField=writer_field,
                       currentPidMatch=None if writer=='unknown' or current_pid is None else writer==str(current_pid))
            birth=row.get('writerProcessCreation100ns')
            birth_known=(isinstance(birth,str) and len(birth)<=20 and birth.isascii() and birth.isdecimal() and int(birth)>0 and int(birth)<(1<<64))
            ref['writerProcessCreation100ns']=birth if birth_known else None
            ref['writerLifetimeKnown']=writer!='unknown' and birth_known
            ref['time']=stamp(row)
            detail=obj(row.get('details'))
            if canonical_log_name(filename)=='sleep-facts.log':
                ready=detail.get('readiness',detail.get('wakeReadiness'))
                if isinstance(ready,dict):
                    observations.observe(ref,row,detail,ready)
                receipt=obj(detail.get('receipt'))
                contract=obj(receipt.get('rescopeContract'))
                if contract:
                    fingerprints.observe(ref,row,receipt,contract)
            result=obj(row.get('result'))
            if canonical_log_name(filename)=='steam-original-recovery.jsonl' and result:
                engine.observe(ref,row,detail,result)
            # Only the named producer field carries this schema. Numeric values
            # elsewhere are never guessed to be an error domain or code.
            candidates=[('row',row),('details',detail),('details.receipt',obj(detail.get('receipt'))),('result',result)]
            for location,parent in candidates:
                failure=parent.get('steamFailure')
                if isinstance(failure,dict):
                    failures.observe(ref,location,parent,failure)
    virtual=obj(availability.get('virtualAdaptation'));journal=obj(virtual.get('journal'))
    context=obj(virtual.get('frameContextEvidence'))
    context_evidence=project(context,('schemaVersion','snapshotAvailable','snapshotAtomicWithSiblingStats','selectionOnly',
        'knownContextsAtSnapshot','maxCapacityOutcomeWitnesses','scope','reason','outcomeMaskEncoding','rejectedWitnessScope'))
    if context.get('snapshotAvailable') is True:
        for key,limit,fields in (
            ('retainedContexts',64,('contextOrdinal','epoch','power','sourceContinuityGeneration','topologyGeneration','selectedOutcomeMask')),
            ('capacityFirstOutcomes',8,('shape','selectionDecision','epoch','power','sourceContinuityGeneration','topologyGeneration',
                'semanticNeutral','nativeAccepted','sourceAdmitted','commandSequence','wireSampleSequence','wireTickMs',
                'fastReceiptUsed','bindingTruncated','runId','hostInstanceId'))):
            values=context.get(key)
            if isinstance(values,list):
                context_evidence[key]=[project(value,fields) for value in values[:limit]]
                context_evidence[key+'NotInspectedBeyondLimit']=max(0,len(values)-limit)
        if type(context.get('schemaVersion')) is int and context['schemaVersion'] == 1 and 'capacityHighestPowerEvidence' in context:
            context_evidence['capacityHighestPowerEvidence'] = frame_capacity_highest_power_evidence(context['capacityHighestPowerEvidence'])
    if context_evidence:
        context_evidence['auditScope']='bounded producer fields;selection metadata is not durable sampling,SDK,or complete rejected-context history'

    failure_rows,failure_selection=failures.finish(project)
    observation_rows,observation_selection=observations.finish(render_readiness)
    engine_rows,engine_selection=engine.finish(render_engine)
    fingerprint_rows,fingerprint_selection=fingerprints.finish(render_fingerprint)
    return {'schemaVersion':1,'scope':'retained-offline-observations;not-live-authority-or-complete-operation-history',
        'touchHapticFailureAttemptEvidence':haptic_failure_attempt_evidence(members,current_pid),
        'nativePowerAtExport':{'admissionObservation':admission,
            'snapshot':project(native,('known','source','generation','publicationRevision','observedNotificationSequence',
                'phase','suspendSerial','hardwareWritesAllowed','notificationPending')),
            'scope':'one export observation;not admission-at-earlier-action-or-cross-component-atomicity'},
        'dispatchAtExport':project(availability.get('sleepPowerDispatch'),('issued','claimed','completed','pendingSuspend',
            'consumerActive','failedPosts','diagnosticHistoryEvictions','recentDeliveries','clockScope','elapsedScope')),
        'dispatchCompletionScope':'dispatcher-handler-return;not-child-Job-provider-SDK-cleanup-or-game-input-success',
        'rendererAuthority':False,'inspectedMembers':inspected,'currentProcessExports':process_exports,'consumerReadinessObservations':observation_rows,
        'nativeEngineReceipts':engine_rows,'reportedSteamFailures':failure_rows,'reportedSteamFailuresSelection':failure_selection,'methodContractFingerprints':fingerprint_rows,
        'shortcutReadEvidence':decky_shortcut_read_evidence(members,current_pid),
        'commonObservationSelection':{'consumerReadinessObservations':observation_selection,'nativeEngineReceipts':engine_selection,'methodContractFingerprints':fingerprint_selection},
        'moduleProfilePreparationEvidence':module_profile_preparation_evidence(members,current_pid),
        'exportCopyFailureEvidence':export_copy_failure_evidence(members,current_pid),
        'hidHideWriterObservationEvidence':hidhide_writer_observation_evidence(members,current_pid),
        'hidHideOutputBytesEvidence':hidhide_output_bytes_evidence(members,current_pid),
        'connectionTruthLogEvidence':connection_truth_log_evidence(members,current_pid),
        'controllerLocalVisibilityEvidence':controller_local_visibility_evidence(members,current_pid),
        'nativeControlWindowEvidence':native_control_window_evidence(members,current_pid),
        'nativeBusRateEvidence':native_bus_rate_evidence(members),
        'nativeForegroundHelperEvidence':native_foreground_helper_evidence(members,current_pid),
        'nativeHttpFailureEvidence':native_http_failure_evidence(members,current_pid),
        'nativeSensorRetirementFailureEvidence':native_sensor_retirement_evidence(members,current_pid),
        'nativeIpcFailureEvidence':native_ipc_failure_evidence(members,current_pid),
        'nativeFiniteOutcomeEvidence':native_finite_outcome_evidence(members,current_pid),
        'calibrationRestoreEvidence':calibration_restore_evidence(members,current_pid),
        'menuInputTraceEvidence':menu_input_trace_evidence(members,current_pid),
        'menuCompositionEvidence':menu_composition_evidence(members,current_pid),
        'powerProjectionFailureEvidence':power_projection_failure_evidence(members,current_pid),
        'powerLeafConsumerFailureEvidence':power_leaf_failure_evidence(members,current_pid),
        'gyroPowerObservationEvidence':gyro_power_observation_evidence(members,current_pid),
        'sessionMarkerDiagnosticExceptionEvidence':session_marker_diagnostic_exception_evidence(availability,current_pid),
        'sessionMarkerEvidence':session_marker_evidence(members,current_pid),
        'powerConsumerBoundaryEvidence':power_consumer_boundary_evidence(members,current_pid),
        'modelFactsProcessExportReceipt':project(obj(availability.get('modelAdaptation')).get('currentProcessFactsExport'),('schemaVersion','saved','file','bytes','maxBytes','maxRecordBytes','maxKnownFacts','snapshotAvailable','knownFactsAtSnapshot','writtenFacts','omittedFacts','retainedSnapshotRowsComplete','sourceFirstWitnessCoverageComplete','reason')),
        'modelSignalsProcessExportReceipt':project(obj(availability.get('modelAdaptation')).get('currentProcessSignalsExport'),('schemaVersion','saved','file','bytes','maxBytes','maxRecordBytes','maxKnownFacts','snapshotAvailable','knownFactsAtSnapshot','writtenFacts','omittedFacts','retainedSnapshotRowsComplete','sourceFirstWitnessCoverageComplete','reason')),
        'virtualProcessExportReceipt':project(virtual.get('currentProcessExport'),('schemaVersion','saved','file','bytes','maxBytes','maxRecordBytes','maxKnownFacts','snapshotAvailable','knownFactsAtSnapshot','writtenFacts','omittedFacts','retainedSnapshotRowsComplete','sourceFirstWitnessCoverageComplete','reason')),
        'virtualJournalCoverage':project(journal,('budgetFull','budgetOmissions','committedThisProcess','completeHistory',
            'lastObservedJournalBytes','maxBytes','writeFailures')),
        'virtualFrameContextEvidence':context_evidence,
        'virtualFrameCoverage':project(virtual,('frameKnownContexts','frameMaxContexts','frameContextLimitReached',
            'frameCapacityDrops','frameSelected','frameBuildFailures','stepBuildFailures')),
        'missingMeans':'unknown;zero-writes-or-zero-writeFailures-does-not-imply-complete-evidence',
        'joinRules':'writer PID+birth where recorded,then native generation+consumer epoch/eventId;no manifest birth backfill;rotations remain independent'}

class VirtualRecoveryFirstWitnessEvidence:
    """Bounded per-member summaries of recorded first steps; no runtime authority."""
    events = ('input-host-runtime-recovery', 'input-host-recovery-exhausted', 'input-host-recovery-fallback')
    max_members, max_observations, max_groups, max_text = 8, 8, 16, 128

    def __init__(self):
        self.members_seen = self.rows_seen = 0
        self.head, self.tail = [], collections.deque(maxlen=self.max_members // 2)

    @staticmethod
    def uint(value, bits=64, positive=False):
        return type(value) is int and int(positive) <= value < (1 << bits)

    @staticmethod
    def birth(value):
        return (isinstance(value, str) and 0 < len(value) <= 20 and value.isascii() and value.isdecimal()
                and 0 < int(value) < (1 << 64))

    @staticmethod
    def equal(a, b):
        return type(a) is type(b) and a == b

    @classmethod
    def project(cls, value, spec):
        reported, states = {}, {}
        for key, kind in spec.items():
            if key not in value:
                states[key] = 'missing'
                continue
            part = value[key]
            if part is None:
                reported[key] = None; states[key] = 'null'
                continue
            valid = (cls.uint(part) if kind == 'uint' else type(part) is bool if kind == 'bool'
                     else isinstance(part, str))
            if not valid:
                states[key] = 'invalid-type-or-range'
            elif kind == 'text' and len(part) > cls.max_text:
                states[key] = 'overlong-text'; reported[key] = part[:cls.max_text]
            else:
                states[key] = 'recorded'; reported[key] = part
        return {'reported': reported, 'fieldStates': states,
                'unprojectedFieldCount': len(value) - sum(key in value for key in spec)}

    @classmethod
    def observation(cls, filename, line, row, metadata):
        obj = lambda value: value if isinstance(value, dict) else {}
        unique = obj(row.get('uniqueKey')); identity = obj(unique.get('identity')); sample = obj(row.get('firstSample'))
        event, origin = identity.get('event'), sample.get('observedFrom')
        if event not in cls.events and origin not in cls.events:
            return None, None
        producer = obj(sample.get('producer')); context = obj(identity.get('recoveryOperationContext'))
        shape = obj(identity.get('recoveryStepShape')); issues = []
        process = canonical_log_name(filename) == 'virtual-adaptation-session.jsonl'
        layout = (row.get('record') == 'first_witness' if process else
                  row.get('schema') == 'ymcc.model-adaptation/1' and row.get('event') == 'virtual-adaptation-witness')
        layout = (layout and row.get('category') == 'virtual-step-witness'
                  and type(unique.get('schema')) is int and unique['schema'] == 1
                  and unique.get('category') == 'virtual-step-witness')
        if not layout:
            issues.append('unsupported-first-witness-layout')
        if event not in cls.events or origin != event:
            issues.append('event-and-observedFrom-disagree-or-unrecognised')
        domain = 'runtime-incident' if event == cls.events[0] else 'terminal-physical-fallback'
        if context.get('serialDomain') != domain:
            issues.append('serial-domain-missing-or-disagrees')
        pid, birth = context.get('writerPid'), context.get('writerProcessCreation100ns')
        if not cls.uint(pid, 32, True):
            issues.append('context-writer-pid-unknown-or-invalid')
        if not cls.birth(birth):
            issues.append('context-writer-birth-unknown-or-invalid')
        for key in ('serial', 'requestId'):
            value, kind = context.get(key), context.get(key + 'Type')
            valid = (cls.uint(value, positive=True) and kind in ('unsigned-integer', 'signed-integer')
                     and (kind != 'signed-integer' or value < (1 << 63)))
            if not valid:
                issues.append('context-' + key + '-unknown-or-invalid')
            if key not in producer or not cls.equal(producer[key], value):
                issues.append('producer-' + key + '-missing-or-disagrees')
        if 'requestId' not in identity or not cls.equal(identity['requestId'], context.get('requestId')):
            issues.append('identity-requestId-missing-or-disagrees')
        if context.get('operationContextKnown') is not True or sample.get('operationContextKnown') is not True:
            issues.append('producer-operation-context-not-explicitly-known')
        # Native Journal writer fields must not contradict the original fact's
        # captured context. Process rows have no per-row writer;never borrow Begin.
        for key in ('writerPid', 'writerProcessCreation100ns'):
            if key in row and not cls.equal(row[key], context.get(key)):
                issues.append('journal-' + key + '-contradicts-context')
        for key in ('stage', 'state', 'reason', 'runId', 'hostEpoch', 'hostInstanceId', 'persona', 'powerGeneration', 'stopInvoked', 'spent'):
            if key in identity and (key not in producer or not cls.equal(identity[key], producer[key])):
                issues.append('identity-producer-' + key + '-disagrees')
        shape_keys = ('recoveryRequestId', 'wave', 'origin', 'nativeWriterAttempt', 'demandRevision', 'via', 'error',
            'pending', 'queued', 'monitoring', 'wavePermit', 'automaticAttemptLimit',
            'ownedCleanupProven', 'ownedVisibilityRestored', 'freshPhysicalRead')
        for key in shape_keys:
            if key not in shape or not cls.equal(shape[key], producer.get(key)):
                issues.append('shape-producer-' + key + '-missing-or-disagrees')
        context_spec = dict(serialDomain='text', writerPid='uint', writerProcessCreation100ns='text', serial='uint',
            requestId='uint', serialType='text', requestIdType='text', operationContextKnown='bool')
        step_spec = dict.fromkeys(('stage', 'state', 'reason', 'origin', 'nativeWriterAttempt', 'via', 'persona',
                                  'runId', 'hostInstanceId', 'policy', 'resultScope', 'physicalReturnScope'), 'text')
        step_spec.update(dict.fromkeys(('hostEpoch', 'powerGeneration', 'recoveryRequestId', 'wave', 'demandRevision',
            'error', 'episodeAttempts', 'rollingCount', 'sequence', 'retryAtTick', 'nextProbeTick', 'observationUntilTick'), 'uint'))
        step_spec.update(dict.fromkeys(('pending', 'queued', 'monitoring', 'wavePermit', 'stopInvoked', 'spent',
            'ownedCleanupProven', 'ownedVisibilityRestored', 'freshPhysicalRead', 'gameInputVerified'), 'bool'))
        projected = cls.project(producer, step_spec)
        # Invalid projected values stay visible but cannot silently qualify a join.
        for key, state in projected['fieldStates'].items():
            if state in ('invalid-type-or-range', 'overlong-text'):
                issues.append('producer-' + key + '-' + state)
        context_state = 'consistent-recorded-tuple' if not issues else 'unknown-or-inconsistent'
        unique_issues = list(dict.fromkeys(issues))
        item = {'file': filename, 'line': line,
            'archiveEntryOccurrence': metadata.get('archiveEntryOccurrence'),
            'event': event if event in cls.events else origin if origin in cls.events else None,
            'storage': 'process-first-witness' if process else 'journal-first-witness',
            'contextState': context_state, 'context': cls.project(context, context_spec),
            'producerStep': projected, 'fieldIssues': unique_issues[:16],
            'fieldIssuesNotListed': max(0, len(unique_issues) - 16),
            'capturedTicks': {'row': cls.project(row, {'capturedTickMs': 'uint'}),
                              'firstSample': cls.project(sample, {'capturedTickMs': 'uint'})},
            'slotOrdinal': row.get('slotOrdinal') if cls.uint(row.get('slotOrdinal'), positive=True) else None,
            'queuePhaseAtSnapshot': row.get('queuePhaseAtSnapshot') if row.get('queuePhaseAtSnapshot') in
                ('pending', 'in-flight', 'drain-resolved') else None,
            'producerOperationContextKnown': sample.get('operationContextKnown') if type(sample.get('operationContextKnown')) is bool else None,
            'gameInputSuccessProven': False, 'physicalReturnProven': False, 'journalPersistenceInferred': False}
        key = (pid, birth, domain, context['serial'], context['requestId']) if not issues else None
        return item, key

    def observe(self, filename, rows, metadata):
        if len(filename) > 256 or '/' in filename or canonical_log_name(filename) not in (
                'virtual-adaptation.log', 'virtual-adaptation-session.jsonl'):
            return
        groups, head, tail = {}, [], collections.deque(maxlen=self.max_observations // 2)
        count = unknown = group_omissions = 0
        event_counts = dict.fromkeys(self.events, 0)
        for line, row in rows:
            item, key = self.observation(filename, line, row, metadata)
            if item is None:
                continue
            count += 1; event_counts[item['event']] += 1
            if len(head) < self.max_observations // 2:
                head.append(item)
            else:
                tail.append(item)
            if key is None:
                unknown += 1
                continue
            if key not in groups:
                if len(groups) == self.max_groups:
                    group_omissions += 1
                    continue
                groups[key] = {'writerPid': key[0], 'writerProcessCreation100ns': key[1], 'serialDomain': key[2],
                    'serial': key[3], 'requestId': key[4], 'observedRows': 0, 'firstLine': line, 'lastLine': line,
                    'events': dict.fromkeys(self.events, 0), 'fullOperationCoverageProven': False}
            group = groups[key]; group['observedRows'] += 1; group['lastLine'] = line; group['events'][item['event']] += 1
        self.members_seen += 1; self.rows_seen += count
        member = {'file': filename, 'archiveEntryOccurrence': metadata.get('archiveEntryOccurrence'),
            'recoveryWitnessRows': count, 'eventCounts': event_counts, 'unknownOrInconsistentContextRows': unknown,
            'observations': head + list(tail), 'observationsNotListed': count - len(head) - len(tail),
            'groups': list(groups.values()), 'consistentRowsWithUnlistedGroup': group_omissions,
            'sourceCoverageProven': False, 'envelopeVerifiedByThisCollector': False}
        if len(self.head) < self.max_members // 2:
            self.head.append(member)
        else:
            self.tail.append(member)

    def evidence(self):
        members = self.head + list(self.tail)
        return {'schemaVersion': 1, 'scope': 'retained-offline-recovery-first-witness-fields;not-runtime-or-physical-authority',
            'membersInspected': self.members_seen, 'members': members, 'membersNotListed': self.members_seen - len(members),
            'recoveryWitnessRowsInspected': self.rows_seen, 'maxListedMembers': self.max_members,
            'maxListedObservationsPerMember': self.max_observations, 'maxListedGroupsPerMember': self.max_groups,
            'maxReportedTextCharacters': self.max_text, 'crossMemberGroupsMerged': False,
            'completeHistoryProven': False, 'producerCollectionCoverageProven': False,
            'processLifetimeProven': False, 'gameInputSuccessProven': False, 'physicalReturnProven': False,
            'readingRules': [
                'only the three exact recovery events and recognised first-witness layouts are interpreted;original bytes remain in the archive',
                'consistent-recorded-tuple means matching reported context/sample fields,not live lifetime verification or execution authority',
                'runtime and terminal-fallback serials use distinct domains;group only within one member and occurrence with exact writer PID/birth+serial/request',
                'missing,invalid or conflicting context never borrows identity from manifest,process Begin,other records or another file',
                'successor-bound is a reported binding step;owned-return-proven is the producer report,not external game input or physical success',
                'process queue phase and copied retained rows do not certify journal persistence;consult separate processExportEnvelopeEvidence and adaptationStoreCoverageEvidence',
                'first/last observations and first16 context groups are bounded;omitted rows/groups/members are explicit and not unique lost operations',
                'row absence cannot prove no recovery,successful recovery or complete collection;no missing stage is reconstructed']}


RECOVERY_DOCUMENTS = {
    'current-export.json': 'snapshot', 'status.json': 'snapshot',
    'last-dual-route-summary.json': 'dual-route', 'last-recovery-summary.json': 'result',
    'last-legacy-dynamic-summary.json': 'result', 'last-semi-static-summary.json': 'result',
}
RECOVERY_ROUTES = ('legacy-dynamic', 'semi-static')
MAX_RECOVERY_DOCUMENTS = 32
MAX_RECOVERY_DOCUMENT_BYTES = 8 * 1024 * 1024
MAX_TERMINAL_REFERENCES = 16


def recovery_route_evidence(documents, members, current_pid, omitted_documents=0):
    """Link retained event references, never reconstruct current route outcomes.

    eventId is an opaque reference. Its digits, manifest PID and file order do
    not supply a missing writer birth or show that a later attempt returned.
    """
    def obj(value):
        return value if isinstance(value, dict) else {}
    def project(value, fields):
        value = obj(value)
        selected, omitted, truncated = {}, [], []
        for key in fields:
            if key not in value:
                continue
            item = value[key]
            if isinstance(item, str):
                selected[key] = item[:512]
                if len(item) > 512:
                    truncated.append(key)
            elif item is None or type(item) is bool or (type(item) is int and -(1 << 63) <= item < (1 << 64)):
                selected[key] = item
            else:
                omitted.append(key)
        if omitted:
            selected['projectionFieldsNotCopied'] = omitted
        if truncated:
            selected['projectionTruncatedFields'] = truncated
        return selected
    def identifier(value):
        return value if isinstance(value, str) and 0 < len(value) <= 256 and value.isprintable() else None
    def number(value, limit):
        if type(value) is int:
            return str(value) if 0 < value < limit else None
        if isinstance(value, str) and len(value) <= 20 and value.isascii() and value.isdecimal():
            return str(int(value)) if 0 < int(value) < limit else None
        return None
    def writer(value, parent=None):
        parent = obj(parent)
        # A partial child identity is one record; never complete it from another writer.
        source = value if any(key in value for key in ('writerPid', 'writerProcessCreation100ns')) else parent
        return {'writerPid': number(source.get('writerPid'), 1 << 32),
                'writerProcessCreation100ns': number(source.get('writerProcessCreation100ns'), 1 << 64)}
    def event_ref(value, *, result=False):
        # An explicit null revokes the old reference; never fall back to trigger.
        value = obj(value)
        direct = identifier(value.get('eventId'))
        trigger = identifier(obj(value.get('trigger')).get('id')) if result else None
        conflict = direct is not None and trigger is not None and direct != trigger
        return (direct if 'eventId' in value else trigger), conflict
    def route_ref(value, fallback=None):
        a, b = value.get('recoveryRoute'), value.get('route')
        if a is not None and b is not None and a != b:
            return None
        value = a if a is not None else b if b is not None else fallback
        return value if value in RECOVERY_ROUTES else None
    result_fields = ('status', 'phase', 'reason', 'driverEntered', 'publisherCallsKnown',
                     'publisherCalls', 'consumed', 'nativeRecovered', 'inputVerified', 'mutationCommitAttempted')
    terminals = collections.defaultdict(list)
    rejected, rejected_count, terminal_count = [], 0, 0
    for filename, rows, metadata in members:
        if canonical_log_name(filename) != 'steam-original-recovery.jsonl':
            continue
        for line, row in rows:
            if row.get('event') != 'engine' or row.get('engineEvent') != 'job-finished':
                continue
            terminal_count += 1
            result = obj(row.get('result'))
            event_id, conflict = event_ref({**row, 'trigger': result.get('trigger')}, result=True)
            route = route_ref(result)
            ref = {'file': filename, 'line': line, **metadata}
            if event_id is None or conflict or route is None or not result:
                rejected_count += 1
                if len(rejected) < MAX_TERMINAL_REFERENCES:
                    rejected.append({**ref, 'reason': 'missing-or-conflicting-event-route-result'})
                continue
            identity = writer(row)
            terminals[(event_id, route)].append({**ref, **identity, 'eventId': event_id, 'route': route,
                'time': stamp(row, filename), 'currentPidMatch': None if identity['writerPid'] is None or current_pid is None
                    else identity['writerPid'] == str(current_pid),
                'writerLifetimeKnown': all(identity.values()), 'result': project(result, result_fields),
                'nativeQualification': project(result.get('failureDiagnostic'), ('stage', 'beforeMemoryRead'))})
    views = []
    for document in documents[:MAX_RECOVERY_DOCUMENTS]:
        value = obj(document.get('value'))
        name = document['file']
        role = RECOVERY_DOCUMENTS[name]
        expected_schema = {'snapshot': 'ymcc.steam-original-recovery.v1',
            'dual-route': 'ymcc.steam-original-dual-route.v1',
            'result': 'ymcc.steam-original-recovery-summary.v1'}[role]
        view = {key: document[key] for key in ('file', 'archiveEntryOccurrence', 'bytes', 'sha256', 'parseError') if key in document}
        view.update(role=role, recognized=value.get('schema') == expected_schema,
                    reported=project(value, ('schema', 'phase', 'reason', 'eventId', 'attemptSequence', 'recoveryRoute',
                        'lastResultState', 'lastResultHistorical', 'terminalEvidenceHistorical', 'engineDispatched')),
                    writer=writer(value), observations=[])
        views.append(view)
        if not view['recognized']:
            continue

        def observe(location, source, event_id, route, reference_basis, historical=None, conflict=False, parent=None):
            identity = writer(source, value if parent is None else parent)
            all_candidates = terminals.get((event_id, route), []) if event_id and route and not conflict else []
            candidates = [item for item in all_candidates if all(
                identity[key] is None or item[key] is None or identity[key] == item[key] for key in identity)]
            distinct_writers = {tuple(item[key] for key in identity) for item in candidates}
            status = ('conflicting-event-identifiers' if conflict else 'missing-event-or-route' if not event_id or not route
                else 'writer-mismatch' if all_candidates and not candidates
                else 'no-retained-terminal-reference' if not candidates
                else 'ambiguous-writer-references' if len(distinct_writers) > 1
                else 'retained-event-route-reference')
            view['observations'].append({'location': location, 'eventId': event_id, 'route': route,
                'referenceBasis': reference_basis, 'reportedHistorical': historical, **identity,
                'reported': project(source, ('phase', 'reason', 'engineDispatched', 'lastResultState',
                    'engineTerminalObserved', 'summaryPersisted', 'diagnosticOnly', 'actionAuthority')),
                'linkStatus': status, 'matchingReferenceCount': len(candidates),
                'writerMismatchCount': len(all_candidates) - len(candidates),
                'terminalReferences': candidates[:MAX_TERMINAL_REFERENCES],
                'terminalReferencesNotListed': max(0, len(candidates) - MAX_TERMINAL_REFERENCES),
                'currentAttemptOutcomeInferred': False})

        if role == 'result':
            event_id, conflict = event_ref(value, result=True)
            observe('result', value, event_id, route_ref(value), 'result.trigger.id-or-explicit-eventId',
                    historical=None, conflict=conflict)
        if role == 'snapshot':
            event_id, conflict = event_ref(value)
            observe('snapshot', value, event_id, route_ref(value), 'snapshot.explicit-eventId', conflict=conflict)
        route_rows = value.get('dualRoutes') if role == 'snapshot' else value.get('routes') if role == 'dual-route' else None
        if isinstance(route_rows, list):
            view['routeRowsNotInspected'] = max(0, len(route_rows) - len(RECOVERY_ROUTES))
            for index, route_row in enumerate(route_rows[:len(RECOVERY_ROUTES)]):
                if not isinstance(route_row, dict):
                    continue
                route = route_ref(route_row)
                event_id, conflict = event_ref(route_row)
                basis = 'route.explicit-eventId'
                if 'eventId' not in route_row and role == 'snapshot':
                    event_id, conflict = event_ref(value)
                    basis = 'legacy-parent-event-reference;not-current-route-attempt-proof'
                observe('routes[' + str(index) + ']', route_row, event_id, route, basis,
                        route_row.get('terminalEvidenceHistorical'), conflict)
                for key in ('terminalEvidence', 'result'):
                    retained = route_row.get(key)
                    if not isinstance(retained, dict):
                        continue
                    event_id, conflict = event_ref(retained, result=key == 'result')
                    retained_route = route_ref(retained, route)
                    if retained_route != route:
                        retained_route = None
                    observe('routes[' + str(index) + '].' + key, retained, event_id, retained_route,
                            'route-retained-reference', route_row.get('terminalEvidenceHistorical'), conflict, parent=writer(route_row, value))
        # Keep retained slots separate from the current snapshot or route attempt.
        slots = obj(value.get('routeTerminals'))
        for route in RECOVERY_ROUTES:
            retained = slots.get(route)
            if isinstance(retained, dict):
                event_id, conflict = event_ref(retained)
                retained_route = route_ref(retained, route)
                observe('routeTerminals.' + route, retained, event_id, retained_route if retained_route == route else None,
                        'producer-retained-slot', historical=None, conflict=conflict)
        retained = value.get('terminalEvidence')
        if isinstance(retained, dict):
            event_id, conflict = event_ref(retained)
            observe('terminalEvidence', retained, event_id, route_ref(retained),
                    'producer-retained-reference', value.get('terminalEvidenceHistorical'), conflict)
        retained_result = value.get('lastResult')
        if isinstance(retained_result, dict):
            event_id, conflict = event_ref(retained_result, result=True)
            observe('lastResult', retained_result, event_id, route_ref(retained_result),
                    'retained-result-reference', value.get('lastResultHistorical'), conflict)
    return {'schemaVersion': 1, 'scope': 'retained-event-route-references;not-current-action-or-resource-return-proof',
            'documents': views, 'documentsNotInspected': omitted_documents + max(0, len(documents) - MAX_RECOVERY_DOCUMENTS),
            'terminalRowsObserved': terminal_count, 'terminalRowsNotCorrelated': rejected_count,
            'uncorrelatedTerminalSamples': rejected, 'maxTerminalReferencesPerObservation': MAX_TERMINAL_REFERENCES,
            'manifestSuppliesWriterIdentity': False, 'eventIdDigitsSupplyWriterBirth': False,
            'currentOutcomeOrPermissionInferred': False, 'resourceReturnInferred': False,
            'rotationDuplicatesMerged': False}


MAX_RECOVERY_TIMING_ROWS = 64
MAX_RECOVERY_TIMING_GROUPS = 64
RECOVERY_TIMING_PHASES = ('initialSnapshot', 'lossQualification', 'normalQuerySetup', 'sourceContext',
                          'preDispatchSnapshot', 'driverAndCleanup', 'postRecoverySnapshot')


def recovery_timing_evidence(members, current_pid):
    """Bounded original timing fields and per-member recorded reference spans.

    No clock read, current-state selection, route inheritance from adjacent
    rows, phase summation or inference of action/child/resource completion.
    """
    phases, clocks, spans = [], [], []
    totals = collections.Counter()

    def retain(target, item, kind):
        totals[kind] += 1
        if len(target) < MAX_RECOVERY_TIMING_ROWS:
            target.append(item)
        else:
            middle = MAX_RECOVERY_TIMING_ROWS // 2
            target[middle:] = target[middle + 1:] + [item]

    def obj(value):
        return value if isinstance(value, dict) else {}

    def uint(value, bits=64, positive=False):
        if type(value) is int:
            return value if int(positive) <= value < (1 << bits) else None
        if isinstance(value, str) and 0 < len(value) <= 20 and value.isascii() and value.isdecimal():
            parsed = int(value)
            return parsed if int(positive) <= parsed < (1 << bits) else None
        return None

    def text(value, maximum):
        return value if isinstance(value, str) and 0 < len(value) <= maximum and value.isprintable() else None

    def scalar_fields(source, integer_fields=(), float_fields=(), clock_fields=(), bool_fields=(), prefix=''):
        source = obj(source)
        selected, issues = {}, []
        for field in (*integer_fields, *float_fields, *clock_fields, *bool_fields):
            if field not in source:
                continue
            value = source[field]
            valid = (type(value) is int and 0 <= value <= 9007199254740991 if field in integer_fields else
                     type(value) in (int, float) and 0 <= value <= 9007199254740991 and math.isfinite(value) if field in float_fields else
                     uint(value) is not None if field in clock_fields else type(value) is bool)
            selected[field] = str(uint(value)) if valid and field in clock_fields else value if valid else None
            if not valid:
                issues.append(prefix + field)
        return selected, issues

    def phase_projection(result):
        result = obj(result)
        projection, issues = {}, []
        for name in ('timingsMs', 'phaseCost'):
            if name not in result:
                continue
            value = result[name]
            if not isinstance(value, dict):
                projection[name] = None
                issues.append(name)
                continue
            selected = {}
            for phase in RECOVERY_TIMING_PHASES:
                if phase not in value:
                    continue
                if name == 'timingsMs':
                    fields, bad = scalar_fields(value, integer_fields=(phase,), prefix='timingsMs.')
                    selected.update(fields)
                elif isinstance(value[phase], dict):
                    fields, bad = scalar_fields(value[phase], integer_fields=('calls', 'clockFailures'),
                        float_fields=('wallMs',), clock_fields=('workerCpu100ns', 'workerCycles'), prefix='phaseCost.' + phase + '.')
                    complete = all(key in fields and fields[key] is not None for key in
                                   ('calls', 'clockFailures', 'wallMs', 'workerCpu100ns', 'workerCycles'))
                    fields['allClockReadsSucceeded'] = fields['clockFailures'] == 0 if complete and fields['calls'] > 0 else None
                    selected[phase] = fields
                else:
                    selected[phase], bad = None, ['phaseCost.' + phase]
                issues.extend(bad)
            projection[name] = selected
            projection[name + 'UnrecognizedKeys'] = sum(key not in RECOVERY_TIMING_PHASES for key in value)
        if 'phaseCostScope' in result:
            projection['phaseCostScope'] = text(result['phaseCostScope'], 256)
            if projection['phaseCostScope'] is None:
                issues.append('phaseCostScope')
        for name, fields in (('observationCost', ('processOpens', 'readCalls', 'readBytes', 'hintSignals', 'processSignals', 'kernelWaits')),
                             ('wakeObservation', ('elapsedMs', 'budgetMs', 'observationWindowMs', 'probes', 'fastProbes', 'settleFastProbes'))):
            if name not in result:
                continue
            if not isinstance(result[name], dict):
                projection[name] = None
                issues.append(name)
                continue
            view, bad = scalar_fields(result[name], integer_fields=fields,
                clock_fields=('workerCpu100ns',) if name == 'observationCost' else
                    ('observationStartFileTime', 'lastHealthyProbeFileTime', 'firstHintFileTime', 'lastHintFileTime'), prefix=name + '.')
            projection[name] = view
            issues.extend(bad)
        return projection, issues

    def ref(filename, line, row, metadata):
        result = obj(row.get('result'))
        writer = uint(row.get('writerPid'), 32, True)
        birth = uint(row.get('writerProcessCreation100ns'), 64, True)
        birth_state = 'missing' if 'writerProcessCreation100ns' not in row else 'reported' if birth else 'invalid-or-null'
        event = text(row.get('eventId'), 256)
        for source in (row, result):
            if 'trigger' in source:
                trigger = obj(source['trigger'])
                identifiers = [trigger[key] for key in ('id', 'eventId') if key in trigger]
                if not identifiers or any(text(value, 256) is None or event != value for value in identifiers):
                    event = None
        routes = [source[key] for source in (row, result) for key in ('route', 'recoveryRoute') if key in source]
        route = routes[0] if routes and all(value in RECOVERY_ROUTES and value == routes[0] for value in routes) else None
        return {'file': filename, 'line': line, **metadata,
            'writerPid': str(writer) if writer else None, 'writerProcessCreation100ns': str(birth) if birth else None,
            'writerBirthState': birth_state, 'writerLifetimeKnown': bool(writer and birth),
            'currentPidMatch': None if writer is None or current_pid is None else str(writer) == str(current_pid),
            'eventId': event, 'route': route, 'time': stamp(row, filename)}

    def endpoint(reference, row):
        value = uint(row.get('atFileTime'), 64, True)
        return {**reference, 'phase': row['phase'], 'atFileTime': str(value) if value else None,
                'clockFieldState': 'missing' if 'atFileTime' not in row else 'reported' if value else 'invalid-or-zero'}

    def finish(groups, boundary):
        for group in groups.values():
            begin, end = group['begin'], group['end']
            status = ('multiple-checking-rows' if group['checkingRows'] > 1 else
                      'intermediate-other-state-phase' if group['otherPhaseRows'] else
                      'missing-checking-reference' if begin is None else
                      'missing-terminal-phase-reference' if end is None else
                      'terminal-reference-before-checking' if end['line'] <= begin['line'] else
                      'clock-field-unavailable' if begin['atFileTime'] is None or end['atFileTime'] is None else
                      'clock-order-invalid' if int(end['atFileTime']) < int(begin['atFileTime']) else
                      'derived-recorded-reference-span')
            view = {**group['identity'], 'begin': begin, 'firstTerminalPhase': end,
                'checkingRows': group['checkingRows'], 'terminalPhaseRows': group['terminalRows'],
                'otherPhaseRows': group['otherPhaseRows'], 'segmentClosedBy': boundary, 'status': status,
                'clockDomain': 'Windows-FILETIME-100ns;reported-wall-clock',
                'correlationScope': 'same-retained-member-and-explicit-writer-event-route;birth-only-if-reported',
                'processLifetimeProven': False, 'actionElapsedOrSuccessInferred': False,
                'intermediateCoverageProven': False}
            if status == 'derived-recorded-reference-span':
                delta = int(end['atFileTime']) - int(begin['atFileTime'])
                whole, part = divmod(delta, 10000)
                view.update(delta100ns=str(delta), referenceSpanMsDecimal=str(whole) + ('.' + ('%04d' % part).rstrip('0') if part else ''))
            retain(spans, view, 'stateSpans')

    terminal_phases = ('healthy', 'recovered-native', 'refused', 'failed')
    for filename, rows, metadata, quality in members:
        if canonical_log_name(filename) != 'steam-original-recovery.jsonl':
            continue
        groups = {}
        unknown = _quality_unknown_lines(quality)
        for line, row, reset in continuity_rows(rows, unknown):
            if reset:
                finish(groups, 'uninterpreted-row-boundary')
                groups.clear()
            reference = ref(filename, line, row, metadata)
            if row.get('event') == 'engine' and row.get('engineEvent') == 'job-finished':
                projection, issues = phase_projection(row.get('result'))
                retain(phases, {**reference, 'status': text(obj(row.get('result')).get('status'), 128),
                    'reported': projection, 'fieldIssues': issues, 'fieldsPresent': bool(projection)}, 'phaseReceipts')
            if row.get('event') == 'engine' and row.get('engineEvent') in (
                    'wake-observation-started', 'late-running-loss-observed', 'source-capture-begin', 'source-capture-end'):
                projection, issues = scalar_fields(row, integer_fields=('elapsedMs', 'debugApiIntervalMs'),
                    clock_fields=('traceBeganTick', 'observedTick', 'observationStartFileTime', 'captureSerial'))
                retain(clocks, {**reference, 'engineEvent': row['engineEvent'], 'reported': projection,
                    'fieldIssues': issues, 'routeFromAdjacentRows': False}, 'clockObservations')
            if row.get('event') != 'state':
                continue
            valid = (row.get('schema') == 'ymcc.steam-original-recovery.v1' and reference['eventId'] and reference['route']
                     and reference['writerPid'] and reference['writerBirthState'] != 'invalid-or-null' and type(row.get('phase')) is str)
            if not valid:
                totals['uncorrelatedStateRows'] += 1
                finish(groups, 'unqualified-state-boundary')
                groups.clear()
                continue
            key = tuple(reference[k] for k in ('writerPid', 'writerProcessCreation100ns', 'eventId', 'route'))
            phase = row['phase']
            if key not in groups:
                if phase != 'checking' and phase not in terminal_phases:
                    continue
                if len(groups) >= MAX_RECOVERY_TIMING_GROUPS:
                    totals['untrackedStateRows'] += 1
                    continue
                groups[key] = {'identity': {k: reference[k] for k in ('file', 'writerPid', 'writerProcessCreation100ns', 'writerBirthState', 'writerLifetimeKnown', 'currentPidMatch', 'eventId', 'route')},
                    'begin': None, 'end': None, 'checkingRows': 0, 'terminalRows': 0, 'otherPhaseRows': 0}
                groups[key]['identity'].update(metadata)
            group = groups[key]
            if phase == 'checking':
                group['checkingRows'] += 1
                if group['begin'] is None:
                    group['begin'] = endpoint(reference, row)
            elif phase in terminal_phases:
                group['terminalRows'] += 1
                if group['end'] is None:
                    group['end'] = endpoint(reference, row)
            else:
                group['otherPhaseRows'] += 1
        finish(groups, 'retained-member-end')
    return {'schemaVersion': 1, 'scope': 'original-recovery-timing-projection;not-action-currentness-or-completion-authority',
        'phaseReceipts': phases, 'clockObservations': clocks, 'stateSpans': spans,
        'observed': {key: totals[key] for key in ('phaseReceipts', 'clockObservations', 'stateSpans', 'uncorrelatedStateRows', 'untrackedStateRows')},
        'notListed': {key: totals[key] - len(value) for key, value in (('phaseReceipts', phases), ('clockObservations', clocks), ('stateSpans', spans))},
        'maxListedPerKind': MAX_RECOVERY_TIMING_ROWS, 'maxTrackedGroupsPerMemberSegment': MAX_RECOVERY_TIMING_GROUPS,
        'selection': 'first32-and-last32-per-kind;member-segments-independent;untrackedStateRows-is-not-unique-events',
        'missingFieldsMean': 'UNKNOWN;missing-timing-is-not-zero', 'phaseTimesSummed': False,
        'routeFromEventIdOrFileOrder': False, 'currentClockRead': False, 'loggerCpuAttribution': False,
        'gameInputOrResourceCompletionInferred': False,
        'readingRules': ['FILETIME reference spans may include suspension or wall-clock adjustment;not CPU or validated action duration',
            'timingsMs is original GetTickCount64-based phase duration;phaseCost.wallMs is worker QPC;they are not interchangeable',
            'workerCpu100ns and workerCycles belong to the recovery worker;not QueryActor,remote Steam threads,whole process or logger cost',
            'clockFailures>0 or missing prevents all-clock-read success claims;zero CPU counters do not prove no work',
            'phase scopes may nest;never sum them into total latency;source-capture and observation clocks retain their own anchors',
            'missing route remains unknown even beside a known route;same eventId can have two routes',
            'spans use first recorded terminal phase after a single checking reference;later phase rows remain counted;no terminal resource proof',
            'unknown lines and unqualified state rows split span history;rotation/duplicate members never join;eventId digits never supply writer birth']}


AUDIT_RECEIPT_NAMES = ('log-export-snapshot.json', 'steam/steam-log-export.json', 'log-export-availability.json', TOUCHPAD_EXPORT_SNAPSHOT)
AUDIT_MANIFEST_NAME = 'YeManCC-export-manifest.txt'
AUDIT_MANIFEST_FIELDS = ('processId', 'nativeSha256', 'exportedAtLocal', 'timeZoneBiasMinutes',
                         'appVersion', 'detailedLogging', 'domainLogsVirtual', 'fanDiagnosticEnabled', 'logExportMode',
                         'inputFrameEvidenceVersion', 'inputFrameEvidenceFile',
                         'frontendLogDeliveryFailures', 'frontendLogDeliveryFailuresSource', 'frontendLogDeliveryFailuresState',
                         'tdpLogDeliveryFailures', 'rendererDiagnosticDeliveryFailures', 'rendererDiagnosticDeliveryFailuresState')
MAX_AUDIT_RECEIPT_BYTES = 8 * 1024 * 1024
MAX_AUDIT_RECEIPT_REFERENCES = 8


class SteamTailMirrorAudit:
    MANIFEST = 'steam-input-export-manifest.json'
    LIMIT = TAIL_LIMIT
    MANIFEST_LIMIT = 64 * 1024
    PAIRS = STEAM_TAIL_PAIRS

    def __init__(self, infos):
        self.sizes = collections.defaultdict(list)
        self.endpoints = {member: tail for tail, source in self.PAIRS
                          for member in (tail, 'steam/logs/' + source)}
        self.read = set()
        self.pending = {}
        self.matches = {}
        self.canonical_hashes = {}
        self.manifest = None
        for info in infos:
            if info.filename in self.endpoints or info.filename == self.MANIFEST:
                sizes = self.sizes[info.filename]
                if len(sizes) < 2:  # Only missing, unique and repeated matter.
                    sizes.append(info.file_size)
        self.candidates = set()
        for tail, source in self.PAIRS:
            left, right = self.sizes[tail], self.sizes['steam/logs/' + source]
            if len(left) == len(right) == 1 and left == right and left[0] <= self.LIMIT:
                self.candidates.add(tail)

    def observe(self, name, data, metadata):
        if metadata.get('containerOnly') or metadata.get('archiveEntryOccurrence') != 0:
            return
        if name == self.MANIFEST and len(self.sizes[name]) == 1:
            self.read.add(name)
            if len(data) <= self.MANIFEST_LIMIT:
                self.manifest = data
        elif name in self.endpoints and len(self.sizes[name]) == 1:
            self.read.add(name)
            tail = self.endpoints[name]
            if name != tail and not self.sizes[tail] and len(data) <= self.LIMIT:
                self.canonical_hashes[tail] = (len(data), hashlib.sha256(data).hexdigest())
            if tail not in self.candidates:
                return
            if tail in self.pending:
                original = self.pending.pop(tail)
                self.matches[tail] = hashlib.sha256(data).hexdigest() if data == original else None
            else:
                self.pending[tail] = data

    def evidence(self, snapshot=None):
        aliases, alias_state = {}, 'not-recorded'
        if isinstance(snapshot, dict) and 'steamTailAliases' in snapshot:
            try:
                aliases = alias_claims(snapshot['steamTailAliases'])
                alias_state = 'reported'
            except ValueError:
                alias_state = 'invalid'
        manifest_state, manifest_rows = 'missing', []
        sizes = self.sizes[self.MANIFEST]
        if len(sizes) > 1:
            manifest_state = 'ambiguous'
        elif sizes:
            if sizes[0] > self.MANIFEST_LIMIT:
                manifest_state = 'over-budget'
            elif self.manifest is None:
                manifest_state = 'read-unavailable'
            else:
                try:
                    value = evidence_json_loads(self.manifest)
                    if (not isinstance(value, dict) or type(value.get('schema')) is not int or value['schema'] != 1 or
                            value.get('scope') != 'steam' or type(value.get('maxBytesPerFile')) is not int or value['maxBytesPerFile'] != self.LIMIT or
                            not isinstance(value.get('files'), list) or len(value['files']) > len(self.PAIRS)):
                        raise ValueError('unsupported tail manifest')
                    manifest_rows, manifest_state = value['files'], 'selected'
                except (ValueError, UnicodeError):
                    manifest_state = 'invalid'
        pairs, redundant = [], 0
        for tail, source in self.PAIRS:
            canonical = 'steam/logs/' + source
            left, right = self.sizes[tail], self.sizes[canonical]
            reported = [row for row in manifest_rows if isinstance(row, dict) and row.get('file') == tail]
            meta = reported[0] if len(reported) == 1 else {}
            if len(left) > 1 or len(right) > 1 or len(reported) > 1:
                state = 'ambiguous'
            elif tail in aliases:
                claim = aliases[tail]
                state = ('verified-alias' if not left and len(right) == 1 and
                         self.canonical_hashes.get(tail) == (claim['bytes'], claim['sha256']) else 'alias-not-verified')
            elif not left or not right:
                state = 'pair-member-missing'
            elif tail not in self.read or canonical not in self.read:
                state = 'read-unavailable'
            elif meta.get('sourceFile') != source or meta.get('saved') is not True or meta.get('available') is not True:
                state = 'tail-provenance-unavailable'
            elif meta.get('truncated') is True:
                state = 'truncated-tail-retained'
            elif (meta.get('truncated') is not False or type(meta.get('copiedBytes')) is not int or
                  type(meta.get('originalBytes')) is not int or type(meta.get('limitBytes')) is not int or meta['limitBytes'] != self.LIMIT or
                  meta['copiedBytes'] != left[0] or meta['originalBytes'] != left[0] or left[0] > self.LIMIT):
                state = 'tail-metadata-inconsistent'
            elif left != right:
                state = 'size-different'
            elif not self.matches.get(tail):
                state = 'bytes-different'
            else:
                state = 'exact-whole-member-match'
                redundant += left[0]
            pairs.append({'tail': tail, 'canonical': canonical, 'state': state,
                          'tailBytes': left[0] if len(left) == 1 else None,
                          'canonicalBytes': right[0] if len(right) == 1 else None,
                          'sharedSha256': (self.matches[tail] if state == 'exact-whole-member-match' else
                                           aliases[tail]['sha256'] if state == 'verified-alias' else None)})
        return {'schemaVersion': 1, 'scope': 'retained-whole-member-bytes;not-live-steam-source-identity',
                'tailManifestState': manifest_state, 'aliasReceiptState': alias_state, 'pairs': pairs, 'exactMatchRedundantBytes': redundant,
                'verifiedAliasCount': sum(pair['state'] == 'verified-alias' for pair in pairs),
                'rawMembersChanged': False, 'zipSizeOrTimeBenefitMeasured': False}


CRITICAL_DIAGNOSTIC_COUNTERS=('offered','accepted','rejectedBusy','rejectedClosed','rejectedFull','rejectedOversized',
    'rejectedCooldown','submitFailed','sinkReturned','sinkThrew','callbackAbandoned','pendingDiscarded','unavailable','preparationFailed')
CRITICAL_DIAGNOSTIC_FAILURE_COUNTERS=('rejectedBusy','rejectedClosed','rejectedFull','rejectedOversized','rejectedCooldown',
    'submitFailed','sinkThrew','callbackAbandoned','pendingDiscarded','unavailable','preparationFailed')
MAX_CRITICAL_IMPLEMENTATION_BYTES=65536


CRITICAL_POST_REASONS=('busy','closed','oversized','cooldown','full','submission-not-accepted')
CRITICAL_POST_KINDS=('native-resident','native-detail','mirror','domain','app','unknown')


def critical_diagnostic_post_witness_evidence(queue):
    """Bounded first claim per post refusal;never a complete failure/loss history."""
    result={'state':'not-reported','rows':[],'fieldIssues':[],'completeFailedEventHistory':False,
        'chronologicalFirstProven':False,'underlyingSubmissionCauseKnown':False,'bodyRead':False}
    if 'postRejectionWitnesses' not in queue:return result
    witness=queue['postRejectionWitnesses']
    if witness is None:result['state']='null-report';return result
    if not isinstance(witness,dict):result['state']='invalid-report';return result
    schema=witness.get('schemaVersion')
    if type(schema) is not int or schema not in (1,2):result['state']='unsupported-contract';return result
    expected={'schemaVersion':schema,
        'claimPolicy':'first-successful-claim-per-post-rejection-reason;not-chronology-complete-history-or-loss-count',
        'nameEncoding':'ascii-printable-replaced-prefix','namePrefixByteLimit':95,
        'resetWithinProcess':False,'snapshotAtomicAcrossReasons':False}
    for key,required in expected.items():
        value=witness.get(key)
        if type(value) is not type(required) or value!=required:result['fieldIssues'].append(key+':missing-or-unsupported')
    if result['fieldIssues']:result['state']='unsupported-contract';return result
    rows=witness.get('rows')
    if not isinstance(rows,list) or len(rows)!=6:result['state']='invalid-row-count';return result
    seen=set();parsed=[]
    for index,row in enumerate(rows):
        prefix='row'+str(index)+':'
        if not isinstance(row,dict):result['fieldIssues'].append(prefix+'not-object');continue
        reason,state=row.get('reason'),row.get('state')
        if not isinstance(reason,str) or reason not in CRITICAL_POST_REASONS or reason in seen:
            result['fieldIssues'].append(prefix+'invalid-or-duplicate-reason');continue
        seen.add(reason)
        if state not in ('empty','publishing','published'):result['fieldIssues'].append(prefix+'invalid-state');continue
        required={'reason','state'}
        if state!='published':
            if set(row)!=required:result['fieldIssues'].append(prefix+'payload-before-publication');continue
            parsed.append({'reason':reason,'state':state});continue
        required.update(('recordKind','nameBytes','namePrefix','nameTruncated','namePrefixSanitized'))
        if schema==2 and reason=='submission-not-accepted':required.add('submissionFailureKind')
        size,name=row.get('nameBytes'),row.get('namePrefix')
        if (set(row)!=required or row.get('recordKind') not in CRITICAL_POST_KINDS
                or type(size) is not int or not 0<=size<(1<<64)
                or not isinstance(name,str) or len(name)>95 or not all(32<=ord(ch)<=126 for ch in name)
                or type(row.get('nameTruncated')) is not bool or type(row.get('namePrefixSanitized')) is not bool):
            result['fieldIssues'].append(prefix+'invalid-published-fields');continue
        if len(name)!=min(size,95) or row['nameTruncated']!=(size>95):
            result['fieldIssues'].append(prefix+'inconsistent-prefix-boundary');continue
        if schema==2 and reason=='submission-not-accepted' and row.get('submissionFailureKind') not in ('returned-false','allocation-exception','standard-exception','unknown-exception'):
            result['fieldIssues'].append(prefix+'invalid-submission-failure-kind');continue
        parsed.append({key:row[key] for key in ('reason','state','recordKind','nameBytes','namePrefix','nameTruncated','namePrefixSanitized')+tuple(['submissionFailureKind'] if schema==2 and reason=='submission-not-accepted' else [])})
    if result['fieldIssues'] or seen!=set(CRITICAL_POST_REASONS):result['state']='invalid-fields';return result
    result.update(state='reported',rows=parsed,reportedSchemaVersion=schema)
    return result


def critical_diagnostic_queue_evidence(manifest_data, selection, current_pid):
    """Existing manifest queue reports only;not file loss or device outcomes."""
    result={'schemaVersion':1,'state':'receipt-unavailable','receiptSelectionStatus':selection.get('status'),
        'archiveEntryOccurrence':selection.get('selectedOccurrence'),'manifestBytes':None,'manifestSha256':None,
        'implementationFieldOccurrences':0,'implementationLine':None,'maxImplementationBytes':MAX_CRITICAL_IMPLEMENTATION_BYTES,
        'reportedPid':None,'reportedProcessCreation100ns':None,'reportedWriterIdentityKnown':False,'currentPidMatch':None,
        'reported':{},'fieldIssues':[],'nonzeroFailureObservations':[],
        'snapshotAtomicAcrossCounters':False,'countsSummedAcrossFailureStages':False,'uniqueLostRowsInferred':False,
        'failedEventNamesKnown':False,'fileFailureProven':False,'durableWriteProven':False,'deviceOrInputRecoveryInferred':False,
        'zeroProvesCompleteHistoryOrHealth':False,'arithmeticIdentitiesEnforced':False,'counterWrapExcluded':False,
        'postRejectionWitnesses':{'state':'not-evaluated','rows':[],'fieldIssues':[],'completeFailedEventHistory':False}}
    if selection.get('status') not in ('selected','selected-with-ambiguous-fields'):return result
    if not isinstance(manifest_data,bytes):result['state']='manifest-bytes-unavailable';return result
    result.update(manifestBytes=len(manifest_data),manifestSha256=hashlib.sha256(manifest_data).hexdigest())
    if len(manifest_data)>MAX_AUDIT_RECEIPT_BYTES:result['state']='manifest-byte-budget-exceeded';return result
    prefix=b'virtualControllerImplementation=';first=None;count=0
    for number,line in enumerate(io.BytesIO(manifest_data),1):
        if number==1:line=line.removeprefix(b'\xef\xbb\xbf')
        if not line.startswith(prefix):continue
        count+=1
        if first is None:first=(number,line[len(prefix):].rstrip(b'\r\n'))
    result['implementationFieldOccurrences']=count
    if count!=1:result['state']='missing-implementation-field' if count==0 else 'ambiguous-implementation-field';return result
    number,payload=first;result['implementationLine']=number
    if len(payload)>MAX_CRITICAL_IMPLEMENTATION_BYTES:result['state']='implementation-byte-budget-exceeded';return result
    try:implementation=evidence_json_loads(payload.decode('utf-8',errors='strict'))
    except (ValueError,UnicodeError) as error:
        result.update(state='implementation-rejected',parseErrorType=evidence_error_type(error),**json_rejection_evidence(error));return result
    if not isinstance(implementation,dict):result['state']='implementation-not-object';return result
    expected_implementation={'schemaVersion':1,'revision':'VC-NATIVE-LIFECYCLE-EVIDENCE-20261024-R2',
        'scope':'running-Native-declared-implementation;not-loaded-byte-hash-device-health-or-consumption-proof'}
    for key,expected in expected_implementation.items():
        value=implementation.get(key)
        if type(value) is not type(expected) or value!=expected:
            result['state']='unsupported-implementation-contract';result['fieldIssues'].append(key+':missing-or-unsupported');return result
    process,birth=implementation.get('processId'),implementation.get('processCreationFileTime')
    if type(process) is int and 0<process<(1<<32):
        result['reportedPid']=str(process)
        if isinstance(current_pid,str) and 0<len(current_pid)<=10 and current_pid.isascii() and current_pid.isdecimal() and 0<int(current_pid)<(1<<32):
            result['currentPidMatch']=int(current_pid)==process
    else:result['fieldIssues'].append('processId:invalid-or-missing')
    if type(birth) is int and 0<birth<(1<<64):result['reportedProcessCreation100ns']=str(birth)
    else:result['fieldIssues'].append('processCreationFileTime:invalid-or-missing')
    result['reportedWriterIdentityKnown']=result['reportedPid'] is not None and result['reportedProcessCreation100ns'] is not None
    if result['currentPidMatch'] is False:result['fieldIssues'].append('reported-pid-disagrees-with-manifest-processId')
    queue=implementation.get('criticalDiagnostics')
    if not isinstance(queue,dict):
        result['state']='missing-queue-report' if 'criticalDiagnostics' not in implementation else 'null-queue-report' if queue is None else 'invalid-queue-report';return result
    if queue.get('policy')!='VC-CRITICAL-DIAGNOSTIC-HANDOFF-20261109-R1' or queue.get('scope')!='diagnostic-value-queue;callback-return-not-file-durability-or-input-recovery':
        result['state']='unsupported-queue-contract';return result
    expected={'policy':'VC-CRITICAL-DIAGNOSTIC-HANDOFF-20261109-R1',
        'scope':'diagnostic-value-queue;callback-return-not-file-durability-or-input-recovery',
        'maxRowsIncludingActive':32,'maxRecordPayloadBytes':65536,'maxPayloadBytesIncludingActive':262144,
        'newWorkerThreads':0,'pollingTimers':0,'snapshotAtomicAcrossCounters':False,'durableWriteProven':False,'gameInputVerified':False}
    for key,required in expected.items():
        value=queue.get(key)
        if type(value) is type(required) and value==required:result['reported'][key]=value
        else:result['fieldIssues'].append(key+':missing-or-unsupported')
    for key in CRITICAL_DIAGNOSTIC_COUNTERS+('pendingRows','payloadBytes'):
        value=queue.get(key)
        if type(value) is int and 0<=value<(1<<64):result['reported'][key]=value
        else:result['fieldIssues'].append(key+':invalid-or-missing')
    for key in ('initialized','closed','sinkActive'):
        value=queue.get(key)
        if type(value) is bool:result['reported'][key]=value
        else:result['fieldIssues'].append(key+':invalid-or-missing')
    for key,limit in (('pendingRows',32),('payloadBytes',262144)):
        if key in result['reported'] and result['reported'][key]>limit:result['fieldIssues'].append(key+':exceeds-declared-limit')
    result['postRejectionWitnesses']=critical_diagnostic_post_witness_evidence(queue)
    result['nonzeroFailureObservations']=[{'counter':key,'reportedCount':result['reported'][key]} for key in CRITICAL_DIAGNOSTIC_FAILURE_COUNTERS if result['reported'].get(key,0)>0]
    result['state']='reported-with-issues' if result['fieldIssues'] else 'reported'
    result['readingRules']=['same manifest field reports a Native writer identity;not independent OS lifetime,loaded-source or action permission',
        'queue rejection,executor submission,preparation,sink exception,callback abandonment and discarded pending data are separate overlapping observation scopes',
        'do not add pendingDiscarded to submitFailed or infer unique lost rows or event names;counter snapshots are not atomic and uint64 wrap is not excluded',
        'accepted and sinkReturned are handoff/callback reports,not actual file write,flush,durability or device recovery',
        'missing legacy fields remain unknown;only the selected implementation line is strict UTF8/JSON decoded;unrelated manifest text is not certified']
    return result


def copy_staged_boundary_evidence(row):
    """Same receipt-row boundary report only;never infer why a file ends there."""
    result = {'state': 'not-recorded', 'reported': {}, 'fieldIssues': []}
    if not isinstance(row, dict):
        return result
    errors, reported = result['fieldIssues'], result['reported']
    status = row.get('status')
    inspection_error = status in ('staged-read-error', 'staged-size-error', 'staged-inspection-error')
    if 'inspectionFailureKind' in row and status != 'staged-inspection-error':
        result['state']='invalid-fields';errors.append('inspection-failure-kind-without-inspection-error');return result
    if inspection_error:
        result.update(state='reported-inspection-error', reported={'status': status})
        kind=row.get('inspectionFailureKind')
        failure={'state':'missing' if 'inspectionFailureKind' not in row else 'null' if kind is None else 'invalid-fields','reportedKind':None}
        result['inspectionFailure']=failure
        if isinstance(kind,str) and kind in ('allocation-exception','standard-exception','unknown-exception'):
            failure.update(state='reported',reportedKind=kind)
        elif 'inspectionFailureKind' in row:
            errors.append('inspection-failure-kind:invalid-or-null');result['state']='invalid-fields'
        return result
    fields = ('format', 'recordBoundary', 'endsWithNewline', 'partialOrInvalidTrailingRecord', 'oversizeTrailingRecordNotParsed')
    if not any(key in row for key in fields):
        return result
    for key in fields:
        if key not in row:
            continue
        value = row[key]
        valid = (value in ('json-lines', 'opaque-file') if key == 'format' and isinstance(value, str) else
                 value in ('partial-or-invalid-tail-retained', 'oversize-tail-retained-not-parsed',
                     'line-terminated', 'valid-json-without-final-newline') if key == 'recordBoundary' and isinstance(value, str) else
                 type(value) is bool if key not in ('format', 'recordBoundary') else False)
        if valid:reported[key] = value
        else:errors.append(key + ':invalid-or-unsupported')
    if errors:
        result['state'] = 'invalid-fields';return result
    if reported.get('format') == 'opaque-file':
        if any(key in row for key in ('recordBoundary', 'partialOrInvalidTrailingRecord', 'oversizeTrailingRecordNotParsed')):
            errors.append('opaque-file-with-json-boundary')
        result['state'] = 'invalid-fields' if errors else 'reported-opaque';return result
    for key in fields:
        if key not in reported:errors.append(key + ':missing')
    if errors:
        result['state'] = 'invalid-fields';return result
    partial, oversize, lf = (reported[key] for key in fields[3:] + ('endsWithNewline',))
    expected = ('partial-or-invalid-tail-retained' if partial else 'oversize-tail-retained-not-parsed' if oversize else
                'line-terminated' if lf else 'valid-json-without-final-newline')
    if reported['recordBoundary'] != expected:errors.append('record-boundary-flags-disagree')
    if partial and oversize:errors.append('two-terminal-shapes-reported')
    if (partial or oversize) and lf:errors.append('terminal-shape-with-final-newline')
    result['state'] = ('invalid-fields' if errors else 'reported-partial-tail' if partial else
        'reported-oversize-tail' if oversize else 'reported-line-terminated' if lf else 'reported-valid-without-final-newline')
    return result


SNAPSHOT_GAP_STATUSES = ('rotated-log-disappeared-before-snapshot','retained-volume-changed-before-snapshot')


def copy_snapshot_gap_evidence(row, receipt, copy_state, copy_reported):
    result={'state':'not-recorded','reported':{},'fieldIssues':[],
            'sourceLossOrDamageInferred':False,'discoveredBasePairProven':False,'missingBytesInferred':False}
    if not isinstance(row,dict) or 'status' not in row:return result
    status=row['status']
    if not isinstance(status,str) or status not in SNAPSHOT_GAP_STATUSES:
        result['state']='not-a-recognised-gap';return result
    result['reported']['status']=status;issues=result['fieldIssues']
    for key in ('listIndex','win32Error','originalBytesRetained'):
        if key not in row:issues.append(key+':missing');continue
        value=row[key]
        valid=type(value) is bool if key=='originalBytesRetained' else type(value) is int and 0<=value<(1<<(32 if key=='win32Error' else 64))
        if valid:result['reported'][key]=value
        else:issues.append(key+':invalid-type-or-range')
    entry=row.get('entry')
    if not isinstance(entry,str) or not 0<len(entry)<=2048 or any(c in entry for c in ('/','\\',':','\x00')):
        issues.append('entry:invalid-basename')
    elif status==SNAPSHOT_GAP_STATUSES[0]:
        if not re.fullmatch(r'.+\.(?:1|prev|previous)',entry):issues.append('entry:not-a-supported-rotation-alias')
    elif entry!='input-host-cost.log.pending' and not COST_ARCHIVE.fullmatch(entry):
        issues.append('entry:not-a-retained-cost-volume')
    if receipt!='log-export-snapshot.json':issues.append('receipt:gap-not-supported-in-Steam-optional-copy-domain')
    if result['reported'].get('win32Error') not in (2,3):issues.append('win32Error:not-a-missing-source-error')
    if result['reported'].get('originalBytesRetained') is not False:issues.append('originalBytesRetained:gap-cannot-claim-retained-bytes')
    if copy_state!='reported-incomplete' or copy_reported.get('stage')!='source-open':issues.append('copyObservation:not-a-coherent-source-open-failure')
    result['state']='invalid-gap' if issues else 'reported-gap'
    return result


def archive_attempt_context_record(document):
    """One staged receipt's reported context;never join files or certify an export."""
    value=document.get('archiveAttemptContext') if isinstance(document,dict) else None
    state='missing' if not isinstance(document,dict) or 'archiveAttemptContext' not in document else 'null' if value is None else 'invalid-type'
    result={'state':state,'reported':{},'fieldIssues':[],'exportOperation':{'state':'not-inspected'},
        'operationRowsJoined':False,'archiveMemberContentBound':False,'currentExportOutcomeProven':False,
        'sourceStabilityOrDurabilityProven':False,'writerLifetimeVerified':False}
    if not isinstance(value,dict):return result
    if type(value.get('schema')) is not int or value['schema']!=1:result['state']='unsupported-schema';return result
    if value.get('scope')!='ordinary-export-staging-attempt;diagnostic-only':result['state']='unsupported-scope';return result
    for key,valid in (('attempt',lambda v:type(v) is int and 1<=v<=2),('maxAttempts',lambda v:type(v) is int and v==2)):
        part=value.get(key)
        if valid(part):result['reported'][key]=part
        else:result['fieldIssues'].append(key+':missing-or-invalid')
    operation=export_operation_record(value);result['exportOperation']=operation
    if operation['state'] not in ('reported-process-operation','reported-pid-operation;birth-unknown'):
        result['fieldIssues'].append('exportOperation:not-coherent')
    result['unprojectedFieldCount']=len(value)-sum(k in value for k in ('schema','scope','attempt','maxAttempts','exportOperation'))
    result['state']='invalid-fields' if result['fieldIssues'] else 'reported-staging-attempt'
    return result


ARCHIVE_ATTEMPT_TIMING_PHASES = ('stagingPreparation', 'sourceSnapshot', 'snapshotEvidenceAndIndex', 'commandPreparation',
                                'tarProcess', 'archiveValidationAndPromotion', 'attemptCleanup', 'retryDelay')


def archive_root_inspection_leaders(work, call):
    result = dict(state='missing', entries=[], limit=None)
    if 'rootInspectionLeaders' not in work:
        return result
    rank = work['rootInspectionLeaders']
    if not isinstance(rank, dict) or type(rank.get('schema')) is not int or rank['schema'] != 1:
        return dict(result, state='unsupported')
    result['state'] = 'invalid-fields'
    rows, reported = rank.get('entries'), call['reported']
    if (type(rank.get('limit')) is not int or rank['limit'] != 8 or not isinstance(rows, list) or
            call['state'] not in ('not-entered', 'reported-completed-calls') or
            len(rows) != min(reported['enteredCalls'], 8)):
        return result
    order, seen = [], set()
    for row in rows:
        if not isinstance(row, dict) or set(row) != {'sourceOrdinal', 'elapsedMs'}:
            return result
        ordinal, elapsed = row['sourceOrdinal'], row['elapsedMs']
        if (type(ordinal) is not int or not 0 <= ordinal < (1 << 64) or ordinal in seen or
                type(elapsed) is not int or not 0 <= elapsed < (1 << 64)):
            return result
        order.append((-elapsed, ordinal));seen.add(ordinal)
    if rows and (rows[0] != {'sourceOrdinal': reported['maxSourceOrdinal'], 'elapsedMs': reported['maxElapsedMs']} or
                 order != sorted(order) or sum(row['elapsedMs'] for row in rows) > reported['elapsedMs'] or
                 (len(rows) == reported['enteredCalls'] and sum(row['elapsedMs'] for row in rows) != reported['elapsedMs'])):
        return result
    return dict(state='reported-ranked-calls' if rows else 'not-entered', entries=rows, limit=8)


def archive_root_inspection_batch(work, call):
    if 'rootInspectionBatch' not in work:
        return dict(state='missing')
    batch = work['rootInspectionBatch']
    invalid = dict(state='invalid-fields')
    if (not isinstance(batch, dict) or type(batch.get('schema')) is not int or batch['schema'] != 1 or
            batch.get('tiePolicy') != 'first-source-ordinal' or call['state'] != 'reported-completed-calls'):
        return invalid
    wall, mode = batch.get('wallMs'), batch.get('mode')
    if type(wall) is not int or not 0 <= wall < (1 << 64) or mode not in ('two-workers', 'inline-batch'):
        return invalid
    reported = call['reported']
    threads = 2 if mode == 'two-workers' else 1
    if (reported['exceptionalCalls'] or not (8 if threads == 2 else 1) <= reported['enteredCalls'] <= 512 or
            reported['maxElapsedMs'] > wall or reported['elapsedMs'] > wall * threads):
        return invalid
    return dict(state='reported-batch-wall', mode=mode, wallMs=wall, tiePolicy=batch['tiePolicy'])


def archive_snapshot_work_record(document):
    """Four original call groups,never current path or whole-export cost proof."""
    view={'state':'missing','reported':{},'calls':{},'fieldIssues':[],
          'fileIdentityOrCrossExportJoinProven':False,'cpuOrFullExportCostProven':False,'operationSuccessInferred':False}
    if not isinstance(document,dict) or 'snapshotWork' not in document:return view
    value=document['snapshotWork']
    if not isinstance(value,dict):view['state']='null' if value is None else 'invalid-type';return view
    if type(value.get('schema')) is not int or value['schema']!=1:view['state']='unsupported-schema';return view
    expected={'scope':'original-source-snapshot-copy-and-first-inspection-call-wall-ms','clock':'GetTickCount64',
              'ordinalScope':'group-local-source-list-index;manifest-zero;not-index-fN-or-file-identity',
              'maxTiePolicy':'first-completed-call','returnedCallProvesSuccess':False,'callsSumIntoFullExportOrCpu':False}
    issues=view['fieldIssues']
    for key,wanted in expected.items():
        if type(value.get(key)) is not type(wanted) or value.get(key)!=wanted:issues.append(key+':missing-or-unsupported')
        else:view['reported'][key]=wanted
    def uint(v):return type(v) is int and 0<=v<(1<<64)
    entered=0
    for name in ('rootCopy','rootInspection','manifestCopy','steamCopy'):
        group=value.get(name);part={'state':'invalid-type','reported':{},'fieldIssues':[]};view['calls'][name]=part
        if not isinstance(group,dict):issues.append(name+':missing-or-invalid');continue
        bad=part['fieldIssues'];out=part['reported']
        for key in ('enteredCalls','returnedCalls','exceptionalCalls'):
            if uint(group.get(key)):out[key]=group[key]
            else:bad.append(key+':missing-or-invalid')
        calls=out.get('enteredCalls')
        if calls==0:
            for key in ('elapsedMs','maxElapsedMs','maxSourceOrdinal'):
                if key in group and group[key] is None:out[key]=None
                else:bad.append(key+':unentered-call-requires-null')
        elif calls is not None:
            entered+=calls
            for key in ('elapsedMs','maxElapsedMs','maxSourceOrdinal'):
                if uint(group.get(key)):out[key]=group[key]
                else:bad.append(key+':missing-or-invalid')
            if uint(out.get('elapsedMs')) and uint(out.get('maxElapsedMs')) and out['maxElapsedMs']>out['elapsedMs']:bad.append('maximum-exceeds-sum')
            if name=='manifestCopy' and (calls!=1 or out.get('maxSourceOrdinal')!=0):bad.append('manifest-not-single-source-zero')
        if all(k in out for k in ('enteredCalls','returnedCalls','exceptionalCalls')) and out['returnedCalls']+out['exceptionalCalls']!=out['enteredCalls']:bad.append('completed-call-count-disagrees')
        part['state']='invalid-fields' if bad else 'not-entered' if calls==0 else 'reported-completed-calls'
        if bad:issues.append(name+':invalid-fields')
    view['rootInspectionLeaders']=archive_root_inspection_leaders(value,view['calls']['rootInspection'])
    view['rootInspectionBatch']=archive_root_inspection_batch(value,view['calls']['rootInspection'])
    if view['rootInspectionBatch']['state']=='invalid-fields':issues.append('rootInspectionBatch:invalid-fields')
    view['unprojectedFieldCount']=len(value)-sum(key in value for key in tuple(expected)+('schema','rootCopy','rootInspection','manifestCopy','steamCopy','rootInspectionLeaders','rootInspectionBatch'))
    view['state']='invalid-fields' if issues else 'not-observed' if entered==0 else 'reported-snapshot-work'
    view['readingRules']=['returned copy may be false;returned inspection may report error;no API result or successful packing is inferred',
        'rootCopy/rootInspection ordinals are positions in original YMCC files list;steamCopy uses separate Steam source list;manifest ordinal is zero',
        'root first inspection differs from later index inspection;copies and full validation remain original;directory creation,receipts and JSON are outside spans',
        'zero ms follows system clock resolution,not zero CPU;missing or unentered timings never become measured zero']
    return view


def archive_index_work_record(document):
    """Two fixed original index-call groups;append ordinals are not file identity."""
    view={'state':'missing','reported':{},'calls':{},'fieldIssues':[],
          'fileIdentityOrCrossExportJoinProven':False,'cpuOrFullExportCostProven':False,'operationSuccessInferred':False}
    if not isinstance(document,dict) or 'indexWork' not in document:return view
    value=document['indexWork']
    if not isinstance(value,dict):view['state']='null' if value is None else 'invalid-type';return view
    if type(value.get('schema')) is not int or value['schema']!=1:view['state']='unsupported-schema';return view
    expected={'scope':'actual-index-inspection-and-digest-call-wall-ms','clock':'GetTickCount64',
              'ordinalScope':'zero-based-append-ordinal;fN-only-if-file-begin-was-written;not-a-file-identity-or-cross-export-join',
              'maxTiePolicy':'first-completed-call','callsSumIntoFullExportOrCpu':False}
    issues=view['fieldIssues']
    for key,wanted in expected.items():
        if type(value.get(key)) is not type(wanted) or value.get(key)!=wanted:issues.append(key+':missing-or-unsupported')
        else:view['reported'][key]=wanted
    for key in ('indexEntered','indexReturned','indexReportedOk'):
        if type(value.get(key)) is bool:view['reported'][key]=value[key]
        else:issues.append(key+':missing-or-invalid')
    entered=value.get('indexEntered');returned=value.get('indexReturned');ok=value.get('indexReportedOk')
    if entered is False and (returned is not False or ok is not False):issues.append('unentered-index-has-result')
    if ok is True and returned is not True:issues.append('ok-index-has-not-returned')
    def uint(v):return type(v) is int and 0<=v<(1<<64)
    for name in ('inspection','digest'):
        group=value.get(name);part={'state':'invalid-type','reported':{},'fieldIssues':[]};view['calls'][name]=part
        if not isinstance(group,dict):issues.append(name+':missing-or-invalid');continue
        bad=part['fieldIssues'];out=part['reported']
        for key in ('enteredCalls','returnedCalls','exceptionalCalls'):
            if uint(group.get(key)):out[key]=group[key]
            else:bad.append(key+':missing-or-invalid')
        calls=out.get('enteredCalls')
        if calls==0:
            for key in ('elapsedMs','maxElapsedMs','maxAppendOrdinal'):
                if key in group and group[key] is None:out[key]=None
                else:bad.append(key+':unentered-call-requires-null')
        elif calls is not None:
            for key in ('elapsedMs','maxElapsedMs','maxAppendOrdinal'):
                if uint(group.get(key)):out[key]=group[key]
                else:bad.append(key+':missing-or-invalid')
            if uint(out.get('elapsedMs')) and uint(out.get('maxElapsedMs')) and out['maxElapsedMs']>out['elapsedMs']:bad.append('maximum-exceeds-sum')
        if all(k in out for k in ('enteredCalls','returnedCalls','exceptionalCalls')) and out['returnedCalls']+out['exceptionalCalls']!=out['enteredCalls']:bad.append('completed-call-count-disagrees')
        if entered is False and calls!=0:bad.append('unentered-index-has-calls')
        part['state']='invalid-fields' if bad else 'not-entered' if calls==0 else 'reported-completed-calls'
        if bad:issues.append(name+':invalid-fields')
    if ok is True and any(part.get('reported',{}).get('exceptionalCalls',0)>0 for part in view['calls'].values()):issues.append('ok-index-has-exceptional-call')
    reuse_keys=('reusedRootInspections','reusedSteamAliasDigests')
    for key in reuse_keys:
        if key not in value:continue
        reuse=value[key]
        if not uint(reuse) or (key=='reusedSteamAliasDigests' and reuse>7):issues.append(key+':invalid-type-or-range')
        else:view['reported'][key]=reuse
    if entered is False and any(key in view['reported'] for key in reuse_keys):issues.append('unentered-index-has-reuse-counter')
    if ok is True and any(key in value for key in reuse_keys) and all(key not in value or key in view['reported'] for key in reuse_keys):
        inspections=view['calls']['inspection']['reported'].get('enteredCalls')
        digests=view['calls']['digest']['reported'].get('enteredCalls')
        if inspections is not None and digests is not None:
            root_reuse,steam_reuse=(view['reported'].get(key,0) for key in reuse_keys)
            if inspections+root_reuse!=digests+steam_reuse:
                issues.append('completed-index-inspection-reuse-and-digest-counts-disagree')
    view['unprojectedFieldCount']=len(value)-sum(key in value for key in tuple(expected)+('schema','indexEntered','indexReturned','indexReportedOk','inspection','digest')+reuse_keys)
    view['state']='invalid-fields' if issues else 'not-entered' if entered is False else 'reported-index-work'
    view['readingRules']=['returned inspection or digest can still report failure;indexReportedOk is only writer return',
        'maxAppendOrdinal refers to this index append order;an early failure can leave fN absent',
        'inspection and digest are measured separately;receipt output,enumeration,JSON serialization and guide output are outside these spans',
        'zero observed milliseconds follows GetTickCount64 resolution,not zero CPU or IO cost']
    return view


def archive_handle_close_record(row):
    result={'state':'missing','reported':{},'handles':{},'fieldIssues':[],
        'resourceRetirementInferred':False,'externalHolderIdentityInferred':False,
        'promotionOrCleanupOutcomeInferred':False,'missingObservationIsSuccess':False}
    if not isinstance(row,dict) or 'handleCloseEvidence' not in row:return result
    value=row['handleCloseEvidence']
    if not isinstance(value,dict):result['state']='null' if value is None else 'invalid-type';return result
    if type(value.get('schema')) is not int or value['schema']!=1:result['state']='unsupported-schema';return result
    required={'scope':'original-archive-handle-CloseHandle-results;not-retirement-proof',
        'resourceRetirementProven':False,'externalHolderIdentityKnown':False}
    issues=result['fieldIssues']
    for key,expected in required.items():
        if key not in value or type(value[key]) is not type(expected) or value[key]!=expected:issues.append(key+':missing-or-unsupported')
        else:result['reported'][key]=value[key]
    for name in ('input','output'):
        item={'state':'missing','reported':{}};result['handles'][name]=item;part=value.get(name)
        if not isinstance(part,dict):issues.append(name+':missing-or-invalid');continue
        attempted=part.get('attempted');accepted=part.get('accepted');error=part.get('win32Error')
        if type(attempted) is not bool:issues.append(name+'.attempted:invalid');item['state']='invalid-fields';continue
        item['reported']['attempted']=attempted
        if not attempted:
            if 'accepted' not in part or accepted is not None or 'win32Error' not in part or error is not None:issues.append(name+':unobserved-results-not-null');item['state']='invalid-fields'
            else:item['reported'].update(accepted=None,win32Error=None);item['state']='not-observed'
        elif type(accepted) is not bool or type(error) is not int or not 0<=error<=0xffffffff or (accepted and error!=0):issues.append(name+':close-result-invalid-or-contradictory');item['state']='invalid-fields'
        else:item['reported'].update(accepted=accepted,win32Error=error);item['state']='close-accepted' if accepted else 'close-refused'
    result['state']='invalid-fields' if issues else 'reported-handle-close-observations'
    result['readingRules']=['only the original CloseHandle call is observed;accepted does not prove zero duplicate/inherited/external references',
        'a later fixture release or successful retry never rewrites an earlier refusal',
        'not-observed may be an earlier snapshot or an unentered path;missing legacy receipt is unknown',
        'close error,promotion error and cleanup error are separate reported API stages']
    return result


def archive_cleanup_record(attempt):
    result={'state':'missing','sites':[],'fieldIssues':[],'observedNonzeroErrorSites':0,'pathAbsenceProven':False,'resourceRetirementProven':False,'otherAttemptOutcomeInferred':False}
    if not isinstance(attempt,dict) or 'cleanupEvidence' not in attempt:return result
    value=attempt['cleanupEvidence']
    if not isinstance(value,dict):result['state']='null' if value is None else 'invalid-type';return result
    if type(value.get('schema')) is not int or value['schema']!=1 or value.get('scope')!='original-five-filesystem-cleanup-sites;not-path-absence-or-resource-return':result['state']='unsupported-schema-or-scope';return result
    issues=result['fieldIssues'];sites=value.get('sites');expected=('prepare-temporary','prepare-staging','promoted-staging','failed-temporary','failed-staging')
    if type(value.get('maxSites')) is not int or value['maxSites']!=5:issues.append('maxSites:unsupported')
    for key in ('pathAbsenceProven','resourceRetirementProven'):
        if value.get(key) is not False:issues.append(key+':unsupported-proof-claim')
    if not isinstance(sites,list):result['state']='invalid-sites-type';return result
    if len(sites)!=5:issues.append('sites:expected-five')
    seen=set()
    for row in sites[:5]:
        item={'state':'invalid-type'};result['sites'].append(item)
        if not isinstance(row,dict):continue
        name=row.get('site')
        if type(name) is not str or name not in expected:item['state']='unsupported-site';continue
        item['site']=name
        if name in seen:issues.append('sites:duplicate-site');item['state']='duplicate-site';continue
        seen.add(name);entered=row.get('attempted');code=row.get('errorValue');category=row.get('errorCategory')
        if type(entered) is not bool:item['state']='invalid-attempted';continue
        item['attempted']=entered
        if not entered:
            item['state']='not-entered' if 'errorValue' in row and 'errorCategory' in row and code is None and category is None else 'inconsistent-unentered-result'
            continue
        if type(code) is not int or not -(1<<31)<=code<(1<<31) or type(category) is not str or category not in ('system','generic','other'):item['state']='invalid-error-fields';continue
        item.update(state='reported-error-code',errorValue=code,errorCategory=category)
        if code:result['observedNonzeroErrorSites']+=1
    if seen!=set(expected):issues.append('sites:fixed-sites-incomplete')
    result['state']='reported-cleanup-call-observations' if not issues and all(q['state'] in ('reported-error-code','not-entered') for q in result['sites']) else 'reported-with-issues'
    return result


def archive_temporary_lookup_record(row, enclosing, cleanup_verified):
    result=dict(reported={},currentPathStateOrUniqueZipProven=False)
    if (not isinstance(row,dict) or set(row)!={'schema','scope','state','win32Error','elapsedMs','bytes'} or
            type(row['schema']) is not int or row['schema']!=1 or
            row['scope']!='owned-attempt-temporary-after-failed-cleanup;metadata-only'):
        return dict(result,state='invalid-schema-or-fields')
    if not cleanup_verified or type(enclosing) is not int:
        return dict(result,state='failed-cleanup-context-unverified')
    label,error,elapsed,size=(row[key] for key in ('state','win32Error','elapsedMs','bytes'))
    if type(error) is not int or not 0<=error<(1<<32) or type(elapsed) is not int or not 0<=elapsed<=enclosing:
        return dict(result,state='invalid-observation')
    if label=='regular-file-metadata':
        valid=error==0 and type(size) is int and 0<=size<(1<<64)
    elif label=='non-regular-path':valid=error==0 and size is None
    elif label=='not-found-at-lookup':valid=error in (2,3) and size is None
    elif label=='attribute-query-failed':valid=error not in (2,3) and size is None
    else:valid=False
    if not valid:return dict(result,state='invalid-observation')
    return dict(result,state='reported-owned-temporary-observation',reported=dict(state=label,win32Error=error,
                                                                                  elapsedMs=elapsed,bytes=size))


FAILED_CLEANUP_SITE_NAMES = ('failed-temporary', 'failed-staging')


def archive_failed_temporary_after_cleanup_record(attempt, cleanup):
    field='failedTemporaryAfterCleanup'
    if field not in attempt:return dict(state='missing',reported={},currentPathStateOrUniqueZipProven=False)
    sites={site['site']:site for site in cleanup['sites'] if 'site' in site}
    tail=[sites.get(key,{}) for key in FAILED_CLEANUP_SITE_NAMES]
    phases=attempt.get('completedStepElapsedMs')
    enclosing=phases.get('attemptCleanup') if isinstance(phases,dict) else None
    verified=(cleanup['state']=='reported-cleanup-call-observations' and attempt.get('attemptBodyReturned') is True and
              sites.get('promoted-staging',{}).get('state')=='not-entered' and
              all(site.get('state')=='reported-error-code' for site in tail) and any(site['errorValue'] for site in tail))
    return archive_temporary_lookup_record(attempt[field],enclosing,verified)


def archive_cleanup_return_record(attempt):
    """Original API return values only;never a fresh path/retirement check."""
    result=dict(state='missing',sites=[],observedReturnSites=None,fieldIssues=[],maxSites=5,
                pathAbsenceProven=False,resourceRetirementProven=False,uniqueFinalZipProven=False)
    baseline=archive_cleanup_record(attempt)
    if baseline['state'] in ('missing','null'):result['state']=baseline['state'];return result
    if baseline['state']!='reported-cleanup-call-observations':result['state']='original-cleanup-wrapper-unrecognised';return result
    result['observedReturnSites']=0
    for row in attempt['cleanupEvidence']['sites']:
        name=row['site'];item=dict(site=name,state='missing',reported={});result['sites'].append(item)
        if 'removalReturnSchema' not in row:continue
        if type(row['removalReturnSchema']) is not int or row['removalReturnSchema']!=1:item['state']='unsupported-return-schema';continue
        fields=('removalReturnObserved','removalApi','removed','removeAllRawReturn','removeAllCountValid')
        if any(k not in row for k in fields):item['state']='missing-return-fields';continue
        observed=row['removalReturnObserved'];api=row['removalApi'];removed=row['removed'];raw=row['removeAllRawReturn'];valid=row['removeAllCountValid']
        if type(observed) is not bool:item['state']='invalid-return-observed';continue
        if not observed:
            if all(row[k] is None for k in fields[1:]):item['state']='not-entered' if not row['attempted'] else 'return-not-observed'
            else:item['state']='conflicting-unobserved-return'
            continue
        if not row['attempted']:item['state']='conflicting-unentered-return';continue
        expected='remove' if name in ('prepare-temporary','failed-temporary') else 'remove_all'
        if api!=expected:item['state']='wrong-api-for-site';continue
        if api=='remove':
            if type(removed) is not bool or raw is not None or valid is not None:item['state']='invalid-remove-return-fields';continue
            item['reported']=dict(api=api,removed=removed)
            item['state']='reported-remove-result' if not (row['errorValue']!=0 and removed) else 'conflicting-error-and-removed'
        else:
            if removed is not None or type(raw) is not int or not 0<=raw<(1<<64) or type(valid) is not bool:item['state']='invalid-remove-all-return-fields';continue
            expectedValid=row['errorValue']==0 and raw!=(1<<64)-1
            item['reported']=dict(api=api,rawReturn=raw,countValid=valid,removedEntryCount=raw if valid else None)
            item['state']='reported-remove-all-result' if valid==expectedValid else 'conflicting-count-validity'
        if item['state'] in ('reported-remove-result','reported-remove-all-result'):result['observedReturnSites']+=1
    states=[v['state'] for v in result['sites']]
    if all(s=='missing' for s in states):result['state']='legacy-return-fields-missing'
    elif all(s in ('reported-remove-result','reported-remove-all-result','not-entered') for s in states):result['state']='reported-original-cleanup-returns'
    elif all(s in ('reported-remove-result','reported-remove-all-result','not-entered','return-not-observed','missing') for s in states):result['state']='reported-with-unknown-return-slots'
    else:result['state']='reported-with-issues'
    return result


def archive_promotion_record(attempt):
    """Original per-attempt API returns;no path query or cross-attempt inference."""
    result=dict(state='missing',promotion={},postPromotionValidation={},fieldIssues=[],
                currentPathAbsenceProven=False,uniqueFinalZipProven=False)
    if not isinstance(attempt,dict) or 'promotionEvidence' not in attempt:return result
    value=attempt['promotionEvidence']
    if value is None:result['state']='null';return result
    if not isinstance(value,dict):result['state']='invalid-type';return result
    if type(value.get('schema')) is not int or value['schema']!=1:result['state']='unsupported-schema';return result
    scope='original-MoveFileExW-and-post-promotion-validation;not-current-path-or-unique-zip-proof'
    if value.get('scope')!=scope:result['state']='unsupported-scope';return result
    issues=result['fieldIssues']
    for field in ('replacementRequested','currentPathAbsenceProven','uniqueFinalZipProven'):
        if value.get(field) is not False:issues.append(field+':not-false')
    promotion=value.get('promotion');validation=value.get('postPromotionValidation')
    if not isinstance(promotion,dict) or not isinstance(validation,dict):result['state']='invalid-fields';issues.append('missing-original-call-slots');return result
    observed=promotion.get('observed');accepted=promotion.get('accepted');error=promotion.get('win32Error')
    entered=validation.get('entered');returned=validation.get('returned');readable=validation.get('readableAndNonempty')
    if type(observed) is not bool:issues.append('promotion:invalid-observed')
    elif not observed:
        if accepted is not None or error is not None or any(k not in promotion for k in ('accepted','win32Error')):issues.append('promotion:conflicting-unobserved')
        else:result['promotion']=dict(state='not-observed')
    elif type(accepted) is not bool or type(error) is not int or not 0<=error<(1<<32):issues.append('promotion:invalid-return')
    elif accepted and error!=0:issues.append('promotion:accepted-with-error')
    else:result['promotion']=dict(state='reported-original-return',accepted=accepted,win32Error=error)
    if 'sharingRetry' in value:
        retry=value['sharingRetry']
        if (not isinstance(retry,dict) or type(retry.get('firstWin32Error')) is not int or retry['firstWin32Error']!=32 or
                type(retry.get('waitElapsedMs')) is not int or not 0<=retry['waitElapsedMs']<(1<<64)):
            issues.append('sharingRetry:invalid-fields')
        elif type(attempt.get('attempt')) is not int or attempt['attempt']!=1 or result['promotion'].get('state')!='reported-original-return':
            issues.append('sharingRetry:without-initial-attempt-move-return')
        else:
            result['sharingRetry']=dict(firstWin32Error=retry['firstWin32Error'],waitElapsedMs=retry['waitElapsedMs'],
                scope='same-validated-candidate;promotion-is-last-move-return;no-new-tar')
            if 'secondWin32Error' in retry or 'secondWaitElapsedMs' in retry:
                secondError=retry.get('secondWin32Error');secondWait=retry.get('secondWaitElapsedMs')
                steps=attempt.get('completedStepElapsedMs')
                if type(secondError) is not int or secondError!=32 or type(secondWait) is not int or not 0<=secondWait<(1<<64):
                    issues.append('sharingRetry:invalid-second-return-or-wait')
                elif isinstance(steps,dict) and 'retryDelay' in steps:
                    issues.append('sharingRetry:second-wait-conflicts-with-repack-delay')
                else:
                    result['sharingRetry'].update(secondWin32Error=secondError,secondWaitElapsedMs=secondWait)
    if type(entered) is not bool:issues.append('validation:invalid-entered')
    elif not entered:
        if returned is not None or readable is not None or any(k not in validation for k in ('returned','readableAndNonempty')):issues.append('validation:conflicting-unentered')
        else:result['postPromotionValidation']=dict(state='not-entered')
    else:
        if observed is not True or accepted is not True:issues.append('validation:without-accepted-promotion')
        if type(returned) is not bool:issues.append('validation:invalid-returned')
        elif returned:
            if type(readable) is not bool:issues.append('validation:invalid-result')
            else:result['postPromotionValidation']=dict(state='reported-original-return',readableAndNonempty=readable)
        elif readable is not None or 'readableAndNonempty' not in validation:issues.append('validation:conflicting-unreturned')
        else:result['postPromotionValidation']=dict(state='entered-return-not-observed')
    result['state']='invalid-fields' if issues else 'reported-original-promotion-observations'
    return result


def archive_output_check_record(attempt, promotion):
    result = dict(state='missing', checks={}, fieldIssues=[])
    if 'outputCheckEvidence' not in attempt:
        return result
    value = attempt['outputCheckEvidence']
    if not isinstance(value, dict):
        result['state'] = 'null' if value is None else 'invalid-type'
        return result
    if (type(value.get('schema')) is not int or value['schema'] != 1 or
            value.get('scope') != 'original-archive-output-checks;not-zip-integrity-or-current-path-proof'):
        result['state'] = 'unsupported-schema-or-scope'
        return result
    issues = result['fieldIssues']
    required = ('state', 'sizeBytes', 'filesystemErrorValue', 'filesystemErrorCategory')
    retry_fields = ('firstFinalSharingCheck', 'finalSharingWaitMs')
    retry_reported = any(key in value for key in retry_fields)
    temp_retry_reported = any(key in value for key in ('firstTemporarySharingCheck', 'temporarySharingWaitMs'))
    names = ('temporary', 'final', 'firstFinalSharingCheck') if retry_reported else ('temporary', 'final')
    if temp_retry_reported:
        names += ('firstTemporarySharingCheck',)
    for name in names:
        row = value.get(name)
        if not isinstance(row, dict) or not all(key in row for key in required):
            issues.append(name + ':missing-fields')
            continue
        state, size, error, category = (row[k] for k in required)
        if state in ('file-status-error', 'file-size-error'):
            valid = (size is None and type(error) is int and -(1 << 31) <= error < (1 << 31) and
                     error != 0 and category in ('system', 'generic', 'other'))
        else:
            if state in ('not-entered', 'not-regular-file'):
                size_valid = size is None
            elif state == 'empty-file':
                size_valid = type(size) is int and size == 0
            elif state in ('stream-open-failed', 'stream-opened-nonempty'):
                size_valid = type(size) is int and 0 < size < (1 << 64)
            else:
                size_valid = False
            valid = size_valid and error is None and category is None
        open_error = row.get('openWin32Error')
        if 'openWin32Error' in row:
            valid = valid and state == 'stream-open-failed' and type(open_error) is int and 0 < open_error < (1 << 32)
        if valid:
            result['checks'][name] = {key: row[key] for key in required}
            if open_error is not None:
                result['checks'][name]['openWin32Error'] = open_error
        else:
            issues.append(name + ':unsupported-or-inconsistent-result')
    if all(name in result['checks'] for name in ('temporary', 'final')):
        temp = result['checks']['temporary']['state']
        final = result['checks']['final']['state']
        if final != 'not-entered' and temp != 'stream-opened-nonempty':
            issues.append('final-without-opened-temporary')
        if promotion['state'] == 'reported-original-promotion-observations':
            move = promotion['promotion']
            checked = promotion['postPromotionValidation']
            if (move['state'] == 'reported-original-return') != (temp == 'stream-opened-nonempty'):
                issues.append('temporary-check-disagrees-with-move')
            if checked['state'] == 'reported-original-return':
                if final == 'not-entered' or (final == 'stream-opened-nonempty') != checked['readableAndNonempty']:
                    issues.append('final-check-disagrees-with-promotion-validation')
            elif final != 'not-entered':
                issues.append('final-check-without-promotion-validation-return')
        if retry_reported:
            first = result['checks'].pop('firstFinalSharingCheck', {})
            wait = value.get('finalSharingWaitMs')
            if (first.get('openWin32Error') != 32 or type(wait) is not int or not 0 <= wait < (1 << 64) or
                    final == 'not-entered' or promotion['state'] != 'reported-original-promotion-observations' or
                    promotion['postPromotionValidation'].get('state') != 'reported-original-return'):
                issues.append('invalid-final-sharing-recheck')
            else:
                result['finalSharingRecheck'] = dict(firstCheck=first, waitElapsedMs=wait)
        if temp_retry_reported:
            first = result['checks'].pop('firstTemporarySharingCheck', {})
            wait = value.get('temporarySharingWaitMs')
            phases = attempt.get('completedStepElapsedMs')
            validation = phases.get('archiveValidationAndPromotion') if isinstance(phases, dict) else None
            if (first.get('openWin32Error') != 32 or type(wait) is not int or type(validation) is not int or
                    not 0 <= wait <= validation < (1 << 64) or 'retryDelay' in phases or
                    type(attempt.get('attempt')) is not int or attempt['attempt'] != 1 or
                    attempt.get('attemptBodyReturned') is not True or temp == 'not-entered' or
                    promotion['state'] != 'reported-original-promotion-observations' or
                    'secondWaitElapsedMs' in promotion.get('sharingRetry', {})):
                issues.append('invalid-temporary-sharing-recheck')
            else:
                result['temporarySharingRecheck'] = dict(firstCheck=first, waitElapsedMs=wait)
    if issues:
        result.pop('finalSharingRecheck', None)
        result.pop('temporarySharingRecheck', None)
        result.update(state='invalid-fields', checks={})
    else:
        result['state'] = 'reported-original-output-checks'
    return result


def archive_child_failure_record(row, completed):
    value = row['archiveChildFailure']
    invalid = dict(state='invalid-fields', reported={})
    if not isinstance(value, dict):
        return dict(invalid, state='null' if value is None else 'invalid-type')
    if type(value.get('schema')) is not int or value['schema'] != 1:
        return dict(invalid, state='unsupported-schema')
    if value.keys() != {'schema', 'backend', 'completion', 'ran', 'captureFinalizationEntered',
                        'wrapperExitCode', 'win32Error', 'captureChannel', 'retainedOutputBytes'}:
        return invalid
    backend, channel = value['backend'], value['captureChannel']
    compression = row.get('zipCompressionRequested')
    if ('tarProcess' not in completed or 'commandPreparation' not in completed or
            not ((backend == 'tar' and channel == 'stderr' and compression in ('level-4','level-5','tar-default')) or
                 (backend == 'unicode-zip' and channel == 'stdout-and-stderr' and compression == 'dotnet-optimal'))):
        return dict(invalid, state='inconsistent-attempt-context')
    ran, entered, finished = value['ran'], value['captureFinalizationEntered'], value['completion']
    code, error, count = value['wrapperExitCode'], value['win32Error'], value['retainedOutputBytes']
    if (type(ran) is not bool or type(entered) is not bool or type(error) is not int or not 0 <= error < 1 << 32 or
            (entered and (type(count) is not int or not 0 <= count <= 8 << 20)) or
            (not entered and count is not None)):
        return invalid
    if ran:
        if (not entered or type(code) is not int or not 0 < code < 1 << 32 or
                finished not in ('child-exit','wait-failed','deadline','overflow','exit-code-failed')):
            return invalid
        if ((finished == 'child-exit' and error != 0) or
                (finished == 'deadline' and (code != 1460 or error != 0)) or
                (finished == 'wait-failed' and code != 1460) or
                (finished == 'overflow' and (code != 111 or error != 0 or count != 8 << 20)) or
                (finished == 'exit-code-failed' and code != (error or 31))):
            return invalid
    elif (code is not None or (finished == 'setup-resume-thread') != entered or finished not in (
            'setup-create-pipe','setup-create-job','setup-configure-job','setup-create-process','setup-assign-job','setup-resume-thread')):
        return invalid
    return dict(state='reported-original-child-failure', reported=value)


def archive_wall_breakdown(archive_ms, count, attempts):
    """Summarize validated, returned attempts within one archive call."""
    if (type(archive_ms) is not int or not 0 <= archive_ms < 1 << 64 or
            type(count) is not int or count not in (1, 2) or len(attempts) != count):
        return None
    attempt_ms = sum(total for total, _ in attempts)
    if attempt_ms > archive_ms:
        return None
    steps = {}
    for _, phases in attempts:
        for name, elapsed in phases.items():
            step = steps.setdefault(name, dict(elapsedMs=0, attemptsReportingStep=0))
            step['elapsedMs'] += elapsed
            step['attemptsReportingStep'] += 1
    measured_ms = sum(step['elapsedMs'] for step in steps.values())
    return dict(scope='one-archive-call;reported-completed-steps-only;not-export-or-cpu-time',
                archiveCallMs=archive_ms, attemptCount=count, attemptTotalsMs=attempt_ms,
                steps={name: steps[name] for name in ARCHIVE_ATTEMPT_TIMING_PHASES if name in steps},
                outsideAttemptTotalsMs=archive_ms-attempt_ms,
                insideAttemptsNotAssignedToReportedStepsMs=attempt_ms-measured_ms)


def archive_attempt_timing_record(document):
    """At most two attempts; an unentered/uncompleted step has no zero value."""
    view = {'state': 'missing', 'reported': {}, 'attempts': [], 'fieldIssues': [],
            'maxInspectedAttempts': 2, 'sourceAttemptsNotInspected': None,
            'missingPhaseIsZero': False, 'stepsSummedIntoExportTotal': False,
            'currentOutcomeOrCpuOrDurabilityInferred': False, 'sameOperationAcrossReceiptsInferred': False}
    if not isinstance(document, dict) or 'archiveAttemptTiming' not in document:return view
    value = document['archiveAttemptTiming']
    if not isinstance(value, dict):view['state'] = 'null' if value is None else 'invalid-type';return view
    if type(value.get('schemaVersion')) is not int or value['schemaVersion'] != 1:view['state'] = 'unsupported-schema';return view
    expected = {'scope': 'current-archive-call-completed-step-wall-ms', 'clock': 'GetTickCount64',
                'maxAttempts': 2, 'maxPhases': 8, 'attemptTotalIncludesRetryDelay': True,
                'includesArchiveSetupOutsideAttempts': False, 'originalResultOrDurabilityProven': False}
    issues = view['fieldIssues']
    for key, required in expected.items():
        if key not in value or type(value[key]) is not type(required) or value[key] != required:issues.append(key + ':missing-or-unsupported')
        else:view['reported'][key] = value[key]
    observed = value.get('attemptsObserved')
    if type(observed) is int and 0 <= observed <= 2:view['reported']['attemptsObserved'] = observed
    else:issues.append('attemptsObserved:invalid-or-missing')
    rows = value.get('attempts')
    if not isinstance(rows, list):view['state'] = 'attempts-unrecognized';return view
    view['sourceAttemptsNotInspected'] = max(0, len(rows) - 2)
    if len(rows) > 2 or type(observed) is not int or len(rows) != observed:issues.append('attempt-count-disagrees')
    seen = {}
    for index, row in enumerate(rows[:2]):
        item = {'sourceIndex': index, 'state': 'invalid-type', 'reported': {}, 'completedStepElapsedMs': {}, 'fieldIssues': [], 'unrecognizedPhaseCount': 0}
        view['attempts'].append(item)
        if not isinstance(row, dict):continue
        bad = item['fieldIssues'];attempt = row.get('attempt');returned = row.get('attemptBodyReturned')
        if type(attempt) is not int or not 1 <= attempt <= 2:bad.append('attempt:invalid-or-missing')
        else:
            item['reported']['attempt'] = attempt
            if attempt in seen:bad.append('duplicate-attempt');seen[attempt]['fieldIssues'].append('duplicate-attempt');seen[attempt]['state'] = 'invalid-fields'
            else:seen[attempt] = item
            if attempt != index + 1:bad.append('attempt-order-or-gap')
        if type(returned) is bool:item['reported']['attemptBodyReturned'] = returned
        else:bad.append('attemptBodyReturned:invalid-or-missing')
        if 'zipCompressionRequested' in row:
            compression = row['zipCompressionRequested']
            if compression is None or compression in ('level-4','level-5','tar-default','dotnet-optimal'):
                item['reported']['zipCompressionRequested'] = compression
            else:
                bad.append('zipCompressionRequested:invalid')
        total = row.get('totalElapsedMs')
        if returned is False:
            if 'totalElapsedMs' in row and total is None:item['reported']['totalElapsedMs'] = None
            else:bad.append('unreturned-total-not-explicit-null')
        elif returned is True:
            if type(total) is int and 0 <= total < (1 << 64):item['reported']['totalElapsedMs'] = total
            else:bad.append('totalElapsedMs:invalid-or-missing')
        phases = row.get('completedStepElapsedMs')
        if not isinstance(phases, dict):bad.append('completedStepElapsedMs:invalid-or-missing')
        else:
            item['unrecognizedPhaseCount'] = len(phases) - sum(key in phases for key in ARCHIVE_ATTEMPT_TIMING_PHASES)
            for name in ARCHIVE_ATTEMPT_TIMING_PHASES:
                if name not in phases:continue
                elapsed = phases[name]
                if type(elapsed) is int and 0 <= elapsed < (1 << 64):item['completedStepElapsedMs'][name] = elapsed
                else:bad.append(name + ':invalid-type-or-range')
            if type(total) is int and type(returned) is bool and returned and sum(item['completedStepElapsedMs'].values()) > total:bad.append('completed-steps-exceed-attempt-total')
            if attempt == 2 and 'retryDelay' in phases:bad.append('last-attempt-cannot-have-retry-delay')
        if 'archiveChildFailure' in row:
            item['archiveChildFailure'] = archive_child_failure_record(row, item['completedStepElapsedMs'])
        item['cleanup']=archive_cleanup_record(row)
        item['cleanupReturn']=archive_cleanup_return_record(row)
        if 'failedTemporaryAfterCleanup' in row:
            item['failedTemporaryAfterCleanup']=archive_failed_temporary_after_cleanup_record(row,item['cleanup'])
        item['promotion']=archive_promotion_record(row)
        item['outputCheck']=archive_output_check_record(row,item['promotion'])
        if item['cleanup']['state'] not in ('missing','null','reported-cleanup-call-observations'):bad.append('cleanup:invalid-fields')
        item['handleClose']=archive_handle_close_record(row)
        if item['handleClose']['state'] not in ('missing','reported-handle-close-observations'):bad.append('handleClose:invalid-fields')
        item['snapshotWork']=archive_snapshot_work_record(row)
        if item['snapshotWork']['state'] not in ('missing','not-observed','reported-snapshot-work'):bad.append('snapshotWork:invalid-fields')
        if isinstance(phases,dict) and type(phases.get('sourceSnapshot')) is int and item['snapshotWork']['state']=='reported-snapshot-work':
            work=item['snapshotWork']
            batch=work['rootInspectionBatch']
            group_times=[batch['wallMs'] if name=='rootInspection' and batch['state']=='reported-batch-wall' else
                         part.get('reported',{}).get('elapsedMs') for name,part in work['calls'].items()]
            if sum(v for v in group_times if type(v) is int)>phases['sourceSnapshot']:bad.append('snapshot-call-times-exceed-containing-phase')
        item['indexWork']=archive_index_work_record(row)
        if item['indexWork']['state'] not in ('missing','not-entered','reported-index-work'):bad.append('indexWork:invalid-fields')
        if isinstance(phases,dict) and type(phases.get('snapshotEvidenceAndIndex')) is int and item['indexWork']['state']=='reported-index-work':
            call_times=[part.get('reported',{}).get('elapsedMs') for part in item['indexWork']['calls'].values()]
            if all(type(v) is int for v in call_times) and sum(call_times)>phases['snapshotEvidenceAndIndex']:bad.append('index-call-times-exceed-containing-phase')
        item['state'] = 'invalid-fields' if bad else 'reported-completed-steps'
    view['state'] = 'invalid-fields' if issues or any(row['state'] != 'reported-completed-steps' for row in view['attempts']) else 'reported-completed-steps'
    if (document.get('event') == 'log-export-result' and view['state'] == 'reported-completed-steps' and
            all(row['reported']['attemptBodyReturned'] and not row['unrecognizedPhaseCount'] for row in view['attempts'])):
        breakdown = archive_wall_breakdown(document.get('archiveElapsedMs'), document.get('attempts'), [
            (row['reported']['totalElapsedMs'], row['completedStepElapsedMs']) for row in view['attempts']])
        if breakdown is not None:
            view['wallBreakdown'] = breakdown
    view['readingRules'] = ['a completed step only exited its code;success,resource return and file durability are independent',
        'snapshot receipt precedes current evidence/index completion,tar and cleanup;unreported steps remain unknown',
        'attempt total includes its original retry delay;step scopes are sequential but not a full export or CPU measurement',
        'a terminal may first appear in a later export;correlate original operation identity separately,never infer it from timing']
    return view


def previous_archive_attempt_record(document):
    """The reported first failed attempt;does not decide the current archive outcome."""
    value=document.get('previousArchiveAttempt') if isinstance(document,dict) else None
    result={'state':'missing' if not isinstance(document,dict) or 'previousArchiveAttempt' not in document else 'null' if value is None else 'invalid-type',
        'reported':{},'fieldIssues':[],'currentExportOutcomeProven':False,'failureCauseExternallyVerified':False}
    if not isinstance(value,dict):return result
    if type(value.get('schema')) is not int or value['schema']!=1:result['state']='unsupported-schema';return result
    if value.get('scope')!='previous-attempt-only;not-current-export-outcome':result['state']='unsupported-scope';return result
    for key in ('attempt','reason','win32Error','temporaryRemoveError','stagingRemoveError','cleanupOk'):
        item=value.get(key)
        valid=(type(item) is int and item==1 if key=='attempt' else
            isinstance(item,str) and 0<len(item)<=63 if key=='reason' else
            type(item) is bool if key=='cleanupOk' else type(item) is int and 0<=item<=0xffffffff)
        if valid:result['reported'][key]=item
        else:result['fieldIssues'].append(key+':missing-or-invalid')
    fields=result['reported']
    if all(k in fields for k in ('cleanupOk','temporaryRemoveError','stagingRemoveError')) and fields['cleanupOk']!=(fields['temporaryRemoveError']==0 and fields['stagingRemoveError']==0):result['fieldIssues'].append('cleanupOk:contradictory')
    result['state']='invalid-fields' if result['fieldIssues'] else 'reported-previous-failed-attempt'
    return result


SNAPSHOT_WRITER_BOUNDARIES = (('monitorWriterBoundary', 'monitor-detail.log'),
                              ('virtualGamepadWriterBoundary', 'virtual-gamepad.log'),
                              ('focusTraceWriterBoundary', 'focus-trace.log'),
                              ('inputCaptureWriterBoundary', 'input-capture.jsonl'),
                              ('tdpWriterBoundary', 'tdp.log'),
                              ('tdpDetailWriterBoundary', 'tdp-detail.log'),
                              ('nativeDetailWriterBoundary', 'native-detail.log'))


def snapshot_writer_boundary_evidence(observation, filename, entry, copy_state, copy_reported, key, carrier):
    active_carrier = filename == 'log-export-snapshot.json' and entry == carrier
    if not isinstance(observation, dict) or key not in observation:
        return {'state': 'not-recorded'} if active_carrier else None
    value = observation[key]
    stage = copy_reported.get('stage')
    coherent = copy_state in ('reported-complete', 'reported-incomplete')
    expected = {'source-open-failed': stage == 'source-open', 'source-size-failed': stage == 'source-size',
                'captured-under-writer-lock': copy_reported.get('sourceSizeKnown') is True,
                'flush-failed': stage not in (None, 'not-started')}
    if not isinstance(value, str) or value not in expected:
        return {'state': 'invalid-value'}
    if not active_carrier or not coherent or not expected[value]:
        return {'state': 'inconsistent', 'reported': value}
    return {'state': value}


def copy_observation_evidence(receipts, selections):
    """Project already parsed unique receipts; never certify a copy or source."""
    stages = ('not-started', 'source-open', 'source-size', 'size-limit', 'destination-open',
              'source-read', 'destination-write', 'destination-close', 'copied')
    counters = ('sourceBytesAtOpen', 'successfulReadBytes', 'successfulWriteBytes', 'readCalls', 'writeCalls')
    booleans = ('sourceSizeKnown', 'destinationCreated', 'copyComplete',
                'sourceStabilityProven', 'durableWriteProven', 'resourceReturnProven')
    fields = ('schema', 'scope', 'stage') + counters + booleans
    sources, first, last, witnesses = [], [], collections.deque(maxlen=16), {}
    totals, stage_counts = collections.Counter(), collections.Counter()
    boundary_counts, boundary_first, boundary_last = collections.Counter(), {}, {}
    inspection_kind_counts = collections.Counter()
    gap_counts,gap_status_counts=collections.Counter(),collections.Counter()
    gap_first,gap_last={},{}
    writer_boundaries = {key: {'countsByState': {}, 'firstLocationByState': {}}
                         for key, _ in SNAPSHOT_WRITER_BOUNDARIES}
    rows_seen = 0

    def uint(value):
        return type(value) is int and 0 <= value < (1 << 64)

    for filename in ('log-export-snapshot.json', 'steam/steam-log-export.json'):
        selection = selections[filename]
        document = receipts.get(filename)
        source = {'file': filename, 'selectionStatus': selection['status'],
                  'selectedOccurrence': selection['selectedOccurrence'], 'rowsObserved': 0}
        sources.append(source)
        if document is None:
            source['filesFieldState'] = 'unavailable';continue
        if filename=='log-export-snapshot.json':
            source['archiveAttemptContext']=archive_attempt_context_record(document)
            source['previousArchiveAttempt']=previous_archive_attempt_record(document)
            source['archiveAttemptTiming']=archive_attempt_timing_record(document)
        files = document.get('files')
        source['filesFieldState'] = ('missing' if 'files' not in document else 'null' if files is None else
                                    'array' if isinstance(files, list) else 'invalid-type')
        if not isinstance(files, list):
            continue
        for index, row in enumerate(files):
            source['rowsObserved'] += 1
            rows_seen += 1
            issues = []
            item = {'receipt': filename, 'archiveEntryOccurrence': selection['selectedOccurrence'],
                    'fileRowIndex': index, 'location': 'files[' + str(index) + '].copyObservation',
                    'entry': None, 'reported': {}, 'fieldsNotProjected': [], 'fieldIssues': issues}
            if not isinstance(row, dict):
                state, value = 'invalid-row', None
            else:
                entry = row.get('entry')
                if isinstance(entry, str) and len(entry) <= 2048:
                    item['entry'] = entry
                elif 'entry' in row:
                    issues.append('entry:unprojected-type-or-length')
                value = row.get('copyObservation')
                state = ('missing' if 'copyObservation' not in row else 'null' if value is None else
                         'object' if isinstance(value, dict) else 'invalid-type')
            if state == 'object':
                known = type(value.get('schema')) is int and value['schema'] == 1 and value.get('scope') == 'original-shared-copy-calls-only'
                for key in fields:
                    if key not in value:
                        issues.append(key + ':missing');continue
                    part = value[key]
                    valid = (type(part) is int and part == 1 if key == 'schema' else
                             isinstance(part, str) and len(part) <= 128 if key in ('scope', 'stage') else
                             type(part) is bool if key in booleans else
                             part is None or uint(part) if key == 'sourceBytesAtOpen' else uint(part))
                    if valid:
                        item['reported'][key] = part
                    else:
                        item['fieldsNotProjected'].append(key);issues.append(key + ':invalid-type-or-range')
                item['unknownFieldCount'] = len(value) - sum(key in value for key in fields) - sum(
                    key in value for key, _ in SNAPSHOT_WRITER_BOUNDARIES)
                if not known:
                    state = 'unsupported'
                elif issues:
                    state = 'invalid-fields'
                elif value['stage'] not in stages:
                    state = 'unsupported'
                else:
                    stage = value['stage'];size = value['sourceBytesAtOpen']
                    read, written, read_calls, write_calls = (value[key] for key in counters[1:])
                    complete, created, size_known = (value[key] for key in ('copyComplete', 'destinationCreated', 'sourceSizeKnown'))
                    conflict = []
                    if (size_known and size is None) or (not size_known and size is not None):
                        conflict.append('source-size-known-value-disagree')
                    if written > read or (size_known and size is not None and read > size):
                        conflict.append('byte-progress-order')
                    if (read and not read_calls) or (written and not write_calls) or write_calls > read_calls:
                        conflict.append('call-progress-order')
                    if any(value[key] is not False for key in booleans[3:]):
                        conflict.append('unproven-stability-durability-or-resource-claim')
                    if complete != (stage == 'copied'):
                        conflict.append('stage-completion-disagree')
                    before_size = stage in ('not-started', 'source-open', 'source-size')
                    before_destination = before_size or stage in ('size-limit', 'destination-open')
                    if before_size and size_known:
                        conflict.append('size-before-size-stage')
                    if not before_size and not size_known:
                        conflict.append('missing-size-after-size-stage')
                    if before_destination and (created or read or written or read_calls or write_calls):
                        conflict.append('progress-before-destination')
                    if not before_destination and not created:
                        conflict.append('destination-not-created')
                    if stage == 'source-read' and not read_calls:
                        conflict.append('read-stage-without-call')
                    if stage == 'destination-write' and not write_calls:
                        conflict.append('write-stage-without-call')
                    if stage in ('copied', 'destination-close') and (read != size or written != size):
                        conflict.append('full-copy-counts-disagree')
                    if stage in ('copied', 'destination-close') and size == 0 and (read_calls or write_calls):
                        conflict.append('empty-copy-with-io-calls')
                    # These stages come from the fixed positive-size 64KiB copy loop.
                    # Successful reads each produce exactly one write attempt.
                    if stage in ('source-read', 'destination-write', 'destination-close', 'copied'):
                        if read > read_calls * 65536 or written > write_calls * 65536:
                            conflict.append('byte-progress-exceeds-call-capacity')
                        if stage == 'source-read' and (read_calls != write_calls + 1 or read != written
                                or size is None or read >= size):
                            conflict.append('failed-read-stage-progress-disagrees')
                        if stage == 'destination-write' and (read_calls != write_calls or read <= written):
                            conflict.append('failed-write-stage-progress-disagrees')
                        if stage in ('copied', 'destination-close') and read_calls != write_calls:
                            conflict.append('complete-loop-call-counts-disagree')
                        # Zero-length source skips the loop; otherwise every successful
                        # read moves at least one byte and cannot exceed the prefix.
                        successful_reads = read_calls - (stage == 'source-read')
                        if successful_reads > read or write_calls > read:
                            conflict.append('positive-read-count-exceeds-confirmed-bytes')
                    if stage == 'size-limit' and size == 0:
                        conflict.append('empty-source-cannot-exceed-positive-limit')
                    if filename.startswith('steam/') and 'copied' in row:
                        outer = row['copied']
                        if type(outer) is not bool or outer != complete:
                            conflict.append('outer-copied-disagrees-or-invalid')
                    if not filename.startswith('steam/') and complete and 'bytes' in row:
                        if not uint(row['bytes']) or row['bytes'] != size:
                            conflict.append('outer-staged-bytes-disagrees-or-invalid')
                    issues.extend(conflict)
                    state = ('contradictory' if conflict else 'not-started' if stage == 'not-started' else
                             'reported-complete' if complete else 'reported-incomplete')
            for key, carrier in SNAPSHOT_WRITER_BOUNDARIES:
                writer_boundary = snapshot_writer_boundary_evidence(value, filename, item['entry'], state, item['reported'], key, carrier)
                if writer_boundary is not None:
                    item[key] = writer_boundary
                    counts = writer_boundaries[key]['countsByState']
                    status = writer_boundary['state']
                    counts[status] = counts.get(status, 0) + 1
                    writer_boundaries[key]['firstLocationByState'].setdefault(status,
                        {'receipt': filename, 'fileRowIndex': index, 'entry': item['entry']})
            boundary = copy_staged_boundary_evidence(row)
            item['stagedBoundary'] = boundary
            gap=copy_snapshot_gap_evidence(row,filename,state,item['reported'])
            item['snapshotGap']=gap;gap_counts[gap['state']]+=1
            if gap['state'] in ('reported-gap','invalid-gap'):
                descriptor=(filename,gap['reported']['status'],gap['state'])
                gap_first.setdefault(descriptor,(rows_seen,item));gap_last[descriptor]=(rows_seen,item)
                if gap['state']=='reported-gap':gap_status_counts[gap['reported']['status']]+=1
            boundary_counts[boundary['state']] += 1
            if isinstance(row,dict) and row.get('status') in ('staged-read-error','staged-size-error','staged-inspection-error'):
                failure=boundary.get('inspectionFailure',{})
                kind=failure.get('reportedKind') if failure.get('state')=='reported' else 'not-recorded' if failure.get('state')=='missing' else 'invalid-or-inconsistent'
                inspection_kind_counts[kind] += 1
            item['state'] = state
            totals[state] += 1
            stage = item['reported'].get('stage')
            coherent = state in ('not-started', 'reported-complete', 'reported-incomplete')
            if coherent:
                stage_counts[stage] += 1
            # At most2*(9stage+7other-state) witnesses plus32first/last =64.
            descriptor = filename, stage if coherent else state
            witnesses.setdefault(descriptor, (rows_seen, item))
            if boundary['state'] in ('reported-partial-tail', 'reported-oversize-tail', 'reported-inspection-error', 'invalid-fields'):
                descriptor = filename, boundary['state']
                boundary_first.setdefault(descriptor, (rows_seen, item))
                boundary_last[descriptor] = rows_seen, item
            # Gap witnesses borrow only ordinary window references. With no
            # gaps the original head16/tail16 remains; all candidates stay<=80.
            window_capacity=32-len(gap_first)-len(gap_last)
            head_capacity=(window_capacity+1)//2;tail_capacity=window_capacity//2
            if len(first) < head_capacity:
                first.append((rows_seen, item))
            while len(first)>head_capacity:first.pop()
            last.append((rows_seen, item))
            while len(last)>tail_capacity:last.popleft()
    # Fixed copy stage/state witnesses (<=32) and first/last of four boundary
    # anomaly classes per source (<=16) take priority within the original64.
    # Fill remaining places from the original head/tail windows in balance.
    listed = dict(list(witnesses.values()) + list(boundary_first.values()) + list(boundary_last.values())+
                  list(gap_first.values())+list(gap_last.values()))
    for head, tail in zip(first, reversed(last)):
        for ordinal, item in (head, tail):
            if ordinal not in listed and len(listed) < 64:listed[ordinal] = item
    return {'schemaVersion': 1, 'scope': 'producer-reported-copy-calls;not-file-integrity-or-live-source-proof',
        'sources': sources, 'rowsObserved': rows_seen, 'countsByState': dict(totals),
        'coherentReportedStageCounts': dict(stage_counts), 'rows': [listed[key] for key in sorted(listed)],
        **writer_boundaries,
        'maxListedRows': 64, 'rowsNotListed': rows_seen - len(listed),
        'countsByStagedBoundaryState': dict(boundary_counts), 'countsByStagedInspectionFailureKind': dict(inspection_kind_counts),
        'countsBySnapshotGapState':dict(gap_counts),'countsByReportedSnapshotGapStatus':dict(gap_status_counts),
        'gapWitnessRows':len(dict(list(gap_first.values())+list(gap_last.values()))),
        'boundaryWitnessRows': len(dict(list(boundary_first.values()) + list(boundary_last.values()))),
        'maxCandidateSelectionReferences': 80,
        'stagedBoundaryCauseOrSourceDamageInferred': False,
        'selection': 'fixed-copy-state-stage-boundary-and-gap-witnesses;gaps-borrow-window-references;balanced-original-head16-tail16-when-no-gaps;within64',
        'copiedBytesSummed': False, 'uniqueFilesOrAttemptsInferred': False,
        'archiveEntryMatchInferred': False, 'sourceStabilityDurabilityOrResourceReturnProven': False,
        'readingRules': ['missing/null/malformed/unsupported stay unknown;do not replace them with zero or staged length',
            'reported-complete requires internally coherent typed counters;it is a producer report,not copy-content certification',
            'bytes count only successful API returns;zero after failed API does not prove no physical bytes moved',
            'same-basename rows and repeated list entries remain separate references;no cross-file or current-lifetime join',
            'source-open/not-started/source-size may have unknown length;empty known source is a distinct real zero',
            'receipt member selection must be unique;duplicates/unreadable/oversize documents never supply a selected winner',
            'counts include retained receipt rows;head/tail plus fixed witnesses do not prove full unique-type coverage',
            'copy counters and staged JSON boundary are separate reports on the same receipt row;complete byte-copy can retain a partial line',
            'fixed boundary witnesses can replace window rows at64;no current source,packed member match or partial-tail cause is inferred']}


def hidhide_failure_receipt_coverage(availability, selection, current_pid):
    """Current Native diagnostic observations, never missing operation counts."""
    availability = availability if isinstance(availability, dict) else {}
    value = availability.get('hidHideFailureReceiptEvidence')
    view = {'schemaVersion': 1, 'receiptSelectionStatus': selection.get('status'),
        'archiveEntryOccurrence': selection.get('selectedOccurrence'),
        'state': 'missing' if 'hidHideFailureReceiptEvidence' not in availability else 'null' if value is None else 'invalid-type',
        'writerPid': None, 'writerProcessCreation100ns': None, 'writerLifetimeKnown': False,
        'currentPidMatch': None, 'reported': {}, 'fieldIssues': [], 'consistencyProblems': [],
        'countUnit': 'entered failed-receipt destructors;not unique commands,all-call failures or missing log rows',
        'fileWriteProven': False, 'wholeCallFailureHistoryProven': False, 'physicalReturnProven': False,
        'lastWindowIsCurrentPowerInferred': False, 'manifestSuppliesWriterBirth': False,
        'zeroExceptionsProvesSinkHealth': False, 'counterValuesSummedWithRetainedLogRows': False}
    if selection.get('status') != 'selected':
        view['state'] = 'receipt-unavailable'
        return view
    if not isinstance(value, dict):
        return view
    if type(value.get('schemaVersion')) is not int or value['schemaVersion'] != 1:
        view['state'] = 'unsupported-schema'
        return view
    def uint(v):return type(v) is int and 0 <= v < (1 << 64)
    process = value.get('writerPid')
    birth = value.get('writerProcessCreation100ns')
    if type(process) is int and 0 < process < (1 << 32):
        view['writerPid'] = str(process)
        view['currentPidMatch'] = None if current_pid is None else str(process) == str(current_pid)
    else:
        view['fieldIssues'].append('writerPid:invalid-or-missing')
    if isinstance(birth, str) and 0 < len(birth) <= 20 and birth.isascii() and birth.isdecimal() and 0 < int(birth) < (1 << 64):
        view['writerProcessCreation100ns'] = birth
    else:
        view['fieldIssues'].append('writerProcessCreation100ns:invalid-or-missing')
    view['writerLifetimeKnown'] = view['writerPid'] is not None and view['writerProcessCreation100ns'] is not None
    stats = value.get('stats')
    if not isinstance(stats, dict):
        view['state'] = 'stats-unrecognized'
        return view
    if type(stats.get('schemaVersion')) is not int or stats['schemaVersion'] != 1:
        view['state'] = 'unsupported-stats-schema'
        return view
    available = stats.get('snapshotAvailable')
    if type(available) is not bool:
        view['state'] = 'snapshot-availability-unknown'
        return view
    view['reported']['snapshotAvailable'] = available
    if available is False:
        view['state'] = 'snapshot-unavailable'
        if stats.get('reason') == 'busy':view['reported']['reason'] = 'busy'
        return view
    view['state'] = 'reported-stats'
    counts = ('observedFailureReceipts', 'admittedReceipts', 'budgetOmissions', 'diagnosticBodyExceptions')
    for key in counts + ('failureReceiptLimit', 'counterLimit', 'counterSaturated'):
        valid = type(stats.get(key)) is bool if key == 'counterSaturated' else uint(stats.get(key))
        if valid:view['reported'][key] = stats[key]
        else:view['fieldIssues'].append(key + ':invalid-or-missing')
    last = stats.get('lastWindow')
    window = {}
    view['reported']['lastWindow'] = window
    if isinstance(last, dict):
        for key in ('powerKnown', 'powerGeneration', 'observedFailureReceipts', 'admittedReceipts', 'budgetOmissions'):
            v = last.get(key)
            valid = type(v) is bool if key == 'powerKnown' else (v is None or uint(v)) if key == 'powerGeneration' else uint(v)
            if key not in last:valid = False
            if valid:window[key] = v
            else:view['fieldIssues'].append('lastWindow.' + key + ':invalid-or-missing')
    else:
        view['fieldIssues'].append('lastWindow:invalid-or-missing')
    reported = view['reported'];problems = view['consistencyProblems']
    if reported.get('failureReceiptLimit') != 32:problems.append('unexpected-receipt-limit')
    if reported.get('counterLimit') != (1 << 64) - 1:problems.append('unexpected-counter-limit')
    if type(window.get('admittedReceipts')) is int and window['admittedReceipts'] > 32:problems.append('window-admitted-over-limit')
    if window.get('powerKnown') is True and window.get('powerGeneration') is None:problems.append('known-window-power-missing')
    if window.get('powerKnown') is False and (window.get('powerGeneration') is not None or any(window.get(k, 0) != 0 for k in counts[:3])):
        problems.append('unobserved-window-has-values')
    if all(k in reported for k in counts) and reported['diagnosticBodyExceptions'] > reported['admittedReceipts']:
        problems.append('exceptions-over-admissions')
    if reported.get('counterSaturated') is False:
        if all(k in reported for k in counts[:3]) and reported['observedFailureReceipts'] != reported['admittedReceipts'] + reported['budgetOmissions']:
            problems.append('total-count-equation-mismatch')
        if all(k in window for k in counts[:3]) and window['observedFailureReceipts'] != window['admittedReceipts'] + window['budgetOmissions']:
            problems.append('window-count-equation-mismatch')
        if any(reported.get(k) == (1 << 64) - 1 for k in counts):problems.append('saturated-value-without-flag')
    for key in counts[:3]:
        if key in reported and key in window and window[key] > reported[key]:problems.append('window-over-total:' + key)
    if problems or view['fieldIssues']:view['state'] = 'reported-stats-with-issues'
    view['readingRules'] = ['the last window follows the latest failed receipt observation;it is not export-time power',
        'early return or receipt construction failure can bypass this observer;zero is not whole-call coverage',
        'admitted counters advance before payload allocation;logger may swallow sink failures',
        'budgetOmissions counts only denied diagnostic receipts;do not subtract retained rows to invent write loss',
        'saturated counters are lower bounds;non-atomic log-file snapshots are not summed with them']
    return view


HOST_EXIT_REASONS=('shutdown-ordered','parent-exited','terminal-cleanup-complete','terminal-cleanup-failed',
    'write-failed','read-failed','fatal-exception','connect-failed')


def input_host_exit_record(row):
    result={'state':'invalid-record-fields','reported':{},'fieldIssues':[],'settlement':{'state':'not-applicable'},
        'processExitObserved':False,'physicalReturnProven':False,'virtualEndpointRetiredProven':False,
        'externalGameInputProven':False,'nativeWriterIdentityInferred':False,'parentCrashInferred':False,
        'uptimeClock':'UTC-wall-clock-delta;not-monotonic-runtime-or-process-birth-proof'}
    if not isinstance(row,dict):return result
    out=result['reported'];issues=result['fieldIssues'];kind=row.get('kind');reason=row.get('reason')
    out['reasonKnown']=type(reason) is str and reason in HOST_EXIT_REASONS
    if out['reasonKnown']:out['reason']=reason
    if type(reason) is not str or not reason or len(reason)>128:issues.append('reason:missing-or-invalid')
    for key in ('t','uptimeMs'):
        value=row.get(key)
        if type(value) is int and -(1<<63)<=value<(1<<63):out[key]=value
        else:issues.append(key+':missing-or-invalid')
    if 't' in out:
        try:out['reportedUtc']=(datetime.datetime(1970,1,1,tzinfo=datetime.timezone.utc)+datetime.timedelta(milliseconds=out['t'])).isoformat(timespec='milliseconds')
        except OverflowError:issues.append('t:outside-UTC-date-range')
    process=row.get('pid')
    if type(process) is int and 0<process<(1<<32):out['hostPid']=process
    else:issues.append('pid:missing-or-invalid')
    identity=row.get('hostInstanceId');out['hostInstanceIdentityState']='missing' if 'hostInstanceId' not in row else 'not-assigned' if identity=='none' else 'invalid'
    if type(identity) is str and re.fullmatch(r'[0-9a-fA-F]{32}',identity):out.update(hostInstanceIdentityState='reported-label',hostInstanceId=identity)
    elif out['hostInstanceIdentityState']=='invalid':issues.append('hostInstanceId:invalid')
    command=row.get('lastCommand');out['lastCommandKnown']=type(command) is str and command in ('none','SUBMIT_FRAME','HIDHIDE_QUERY_STATE','SHUTDOWN','PREPARE','OPEN','RELEASE','PING','HEALTH','HELLO')
    if out['lastCommandKnown']:out['lastCommand']=command
    if 'kind' not in row:result['state']='legacy-kind-unreported';return result
    if type(kind) is not str or kind not in ('orderly','parent-lost','resource-settlement','fatal'):result['state']='unsupported-kind';return result
    out['kind']=kind
    expected='orderly' if reason=='shutdown-ordered' else 'parent-lost' if reason=='parent-exited' else 'resource-settlement' if reason in ('terminal-cleanup-complete','terminal-cleanup-failed') else 'fatal'
    if kind!=expected:result['state']='contradictory-kind-reason';return result
    result['state']='invalid-record-fields' if issues else 'reported-'+kind
    if kind!='resource-settlement':return result
    receipt={'state':'missing-detail','reported':{},'fieldIssues':[],'managedReleaseCompleteReported':None,
        'processExitObserved':False,'sdkEndpointOrPhysicalReturnProven':False,'sourceAndTerminalExitCodesMerged':False}
    result['settlement']=receipt
    if 'detail' not in row:return result
    detail=row['detail']
    if type(detail) is not str:receipt['state']='invalid-detail-type';return result
    if len(detail)>32768:receipt['state']='byte-limit';return result
    try:
        encoded=detail.encode('utf-8')
        if len(encoded)>32768:receipt['state']='byte-limit';return result
        value=evidence_json_loads(encoded)
    except (ValueError,UnicodeError,RecursionError):receipt['state']='parse-rejected';return result
    if not isinstance(value,dict):receipt['state']='invalid-object';return result
    fields=receipt['reported'];bad=receipt['fieldIssues']
    for key in ('ExitCode','SourceExitCode'):
        n=value.get(key)
        if type(n) is int and -(1<<31)<=n<(1<<31):fields[key]=n
        else:bad.append(key+':missing-or-invalid')
    for key in ('ManagedReleaseComplete','Retried'):
        n=value.get(key)
        if type(n) is bool:fields[key]=n
        else:bad.append(key+':missing-or-invalid')
    for key in ('ReleaseAttempts','DirectReleaseAttempts'):
        n=value.get(key)
        if type(n) is int and (1 if key=='ReleaseAttempts' else 0)<=n<=2:fields[key]=n
        else:bad.append(key+':missing-or-invalid')
    disposition=value.get('ResourceDisposition')
    if type(disposition) is str and disposition in ('managed-release-complete','owner-retained','release-unconfirmed'):fields['ResourceDisposition']=disposition
    else:bad.append('ResourceDisposition:missing-or-invalid')
    sourceReason=value.get('Reason');fields['sourceReasonKnown']=type(sourceReason) is str and sourceReason in HOST_EXIT_REASONS
    if fields['sourceReasonKnown']:fields['Reason']=sourceReason
    if type(sourceReason) is not str or not sourceReason or len(sourceReason)>128:bad.append('Reason:missing-or-invalid')
    for key in ('FirstDetail','FinalDetail'):
        if type(value.get(key)) is not str:bad.append(key+':missing-or-invalid')
    if bad:receipt['state']='invalid-fields';return result
    complete=fields['ManagedReleaseComplete'];source=fields['SourceExitCode'];code=fields['ExitCode'];attempts=fields['ReleaseAttempts'];direct=fields['DirectReleaseAttempts']
    if (complete!=(reason=='terminal-cleanup-complete') or (disposition=='managed-release-complete')!=complete or
            code!=(source if complete or source!=0 else 7) or not 0<=attempts-direct<=1 or fields['Retried']!=(attempts==2)):
        bad.append('settlement:conflicting-current-producer-fields');receipt['state']='contradictory';return result
    receipt.update(state='reported',managedReleaseCompleteReported=complete)
    return result


def input_host_exit_evidence(members):
    kinds=('orderly','parent-lost','resource-settlement','fatal','unrecognised-or-missing')
    states=('invalid-record-fields','legacy-kind-unreported','unsupported-kind','contradictory-kind-reason',
        'reported-orderly','reported-parent-lost','reported-resource-settlement','reported-fatal')
    receiptStates=('not-applicable','missing-detail','invalid-detail-type','byte-limit','parse-rejected','invalid-object','invalid-fields','contradictory','reported')
    banks={kind:{'first':[],'last':collections.deque(maxlen=4),'rows':0} for kind in kinds};witnesses={};counts=collections.Counter();settlements=collections.Counter();managedCounts={"complete":0,"incomplete":0,"unavailable":0};sourceNonzero=terminalNonzero=0;total=0
    for filename,rows,metadata in members:
        if type(filename) is not str or not re.fullmatch(r'input-host-fatal\.log(?:\.(?:prev|previous|[0-9]+))?',filename):continue
        for line,row in rows:
            if not isinstance(row,dict):continue
            total+=1;record=input_host_exit_record(row);state=record['state'];settlement=record['settlement']['state'];assert state in states and settlement in receiptStates;counts[state]+=1;settlements[settlement]+=1
            rawKind=row.get('kind');kind=rawKind if type(rawKind) is str and rawKind in kinds[:4] else kinds[-1];bank=banks[kind];bank['rows']+=1
            managedClass=None
            if kind=='resource-settlement':
                if settlement=='reported':
                    receipt=record['settlement'];complete=receipt['managedReleaseCompleteReported'];fields=receipt['reported'];nonzero=fields['SourceExitCode']!=0
                    managedCounts['complete' if complete else 'incomplete']+=1;sourceNonzero+=nonzero;terminalNonzero+=fields['ExitCode']!=0
                    managedClass=('complete' if complete else 'incomplete')+('-source-nonzero' if nonzero else '-source-zero')
                else:managedCounts['unavailable']+=1
            item={'encounterOrdinal':total,'file':filename[:160],'line':line if type(line) is int and 0<line<(1<<64) else None,'record':record}
            for key in ('archiveEntryIndex','archiveEntryOccurrence'):
                value=metadata.get(key) if isinstance(metadata,dict) else None
                if type(value) is int and 0<=value<(1<<64):item[key]=value
            if len(bank['first'])<4:bank['first'].append(item)
            else:bank['last'].append(item)
            witnesses.setdefault(('state',state),item);witnesses.setdefault(('settlement',settlement),item);witnesses.setdefault(('reason',record['reported'].get('reason','other-or-unavailable')),item)
            if managedClass is not None:witnesses.setdefault(('managed-result',managedClass),item)
    selected={item['encounterOrdinal']:item for bank in banks.values() for item in bank['first']+list(bank['last'])}
    selected.update({item['encounterOrdinal']:item for item in witnesses.values()})
    return {'schemaVersion':1,'scope':'retained-Host-exit-intent-and-managed-cleanup-reports;not-process-or-device-exit-proof',
        'rowsObserved':total,'rowsByReportedKind':{key:banks[key]['rows'] for key in kinds},'rowsByState':dict(counts),'settlementStates':dict(settlements),
        'observations':[selected[k] for k in sorted(selected)],'maxListedObservations':70,'rowsNotListed':total-len(selected),
        'managedCleanupOutcomeRows':managedCounts,'managedCleanupSourceNonzeroRows':sourceNonzero,'managedCleanupTerminalNonzeroRows':terminalNonzero,
        'nestedReceiptByteLimit':32768,'selection':'per5 fixed kind banks:first4,last4;first8 record states,first9 receipt states,first9 reason classes,first4 managed/source result classes',
        'fatalFileNameMeansAllRowsFailed':False,'uniqueCrashesOrProcessesCounted':False,'rowsJoined':False,'completeHistoryProven':False,
        'hostComparedWithNativeManifestPid':False,'currentHostIdentityInferred':False,'processExitObserved':False,'physicalReturnProven':False,'virtualEndpointRetiredProven':False,
        'readingRules':['orderly is an original shutdown report;resource-settlement is a separate managed-cleanup report',
            'parent-lost does not distinguish App close from App crash;fatal names a reported failure,not proof of process death',
            'ManagedReleaseComplete and ExitCode remain separate from SourceExitCode;successful cleanup cannot erase a nonzero source result',
            'Host PID and hostInstanceId are row labels;Native manifest PID and OS process birth are never substituted',
            't is UTC epoch-ms;uptimeMs is a wall-clock delta,not a monotonic lifetime or creation-time witness',
            'raw detail/note and arbitrary exception text are not copied;nested receipt duplicates and excessive bytes remain explicit unknown',
            'absence of a terminal row does not prove the Host is live or that complete history was retained']}


HIDHIDE_WRITER_SCOPE='CLI writer only; no target/physical-return/game proof'
HIDHIDE_WRITER_REASONS=(
    'exact-native-child-and-job-writer-quiescent','exact-owned-CLI-root-and-subtree-exited',
    'empty-history-cache-miss-current-owner-acquired','writer-record-open-exception',
    'recorded-native-owner-alive-or-unknown','recorded-CLI-child-alive-or-unknown',
    'recorded-CLI-subtree-active-or-unknown','atomic-CLI-job-unavailable',
    'CLI-child-identity-not-durably-published')


def hidhide_writer_observation_record(row):
    result={'state':'unsupported-event','reported':{},'fieldIssues':[],
        'writerSettlementReported':False,'physicalReturnProven':False,'virtualTargetRetiredProven':False,
        'externalGameInputProven':False,'liveProcessOrJobStateProven':False,'errorCodeInferred':False}
    if not isinstance(row,dict) or row.get('event')!='hidhide-cli-writer':return result
    if row.get('scope')!=HIDHIDE_WRITER_SCOPE:result['state']='unsupported-scope';return result
    out=result['reported'];issues=result['fieldIssues'];phase=row.get('phase');reason=row.get('reason')
    if phase not in ('settled','deferred','launch-rejected','history-hint') or type(phase) is not str:result['state']='unsupported-phase-or-reason';return result
    out['phase']=phase
    if type(reason) is not str or not reason or len(reason)>256:issues.append('reason:missing-or-invalid')
    out['reasonKnown']=type(reason) is str and reason in HIDHIDE_WRITER_REASONS
    if out['reasonKnown']:out['reason']=reason
    def birth(value):return type(value) is str and 0<len(value)<=20 and value[0]!='0' and value.isascii() and value.isdecimal() and int(value)<(1<<64)
    def process(prefix):
        p=row.get(prefix+'Pid');b=row.get(prefix+'Birth')
        if type(p) is int and p==0 and b=='':out[prefix+'IdentityState']='not-supplied';return 'not-supplied'
        if type(p) is int and 0<p<(1<<32) and birth(b):out.update({prefix+'IdentityState':'reported-tuple',prefix+'Pid':p,prefix+'Birth':b});return 'reported-tuple'
        out[prefix+'IdentityState']='invalid-or-incomplete';issues.append(prefix+'Identity:invalid-or-incomplete');return 'invalid-or-incomplete'
    parent=process('parent');child=process('child');attempt=row.get('attemptId');job=row.get('jobName')
    if attempt=='' and job=='' and parent==child=='not-supplied':out['attemptIdentityState']='not-supplied'
    elif type(attempt) is str and 0<len(attempt)<=100 and re.fullmatch(r'[A-Za-z0-9_{}:.\-]+',attempt) and attempt not in ('.','..') and job=='Local\\YMCC.HidHide.Cli.'+attempt and parent=='reported-tuple':
        out.update(attemptIdentityState='reported-attempt-and-parent',attemptId=attempt,jobNameMatchesAttempt=True)
    else:out['attemptIdentityState']='invalid-or-incomplete';issues.append('attemptIdentity:invalid-or-incomplete')
    if phase=='settled' and (reason not in HIDHIDE_WRITER_REASONS[:2] or out['attemptIdentityState']!='reported-attempt-and-parent'):issues.append('settled:unrecognised-reason-or-missing-attempt')
    if phase=='history-hint' and (reason!=HIDHIDE_WRITER_REASONS[2] or out['attemptIdentityState']!='not-supplied'):issues.append('history-hint:conflicting-contract')
    result['state']='invalid-fields' if issues else {'settled':'reported-writer-settlement','deferred':'reported-deferred','launch-rejected':'reported-launch-rejected','history-hint':'reported-history-hint'}[phase]
    result['writerSettlementReported']=result['state']=='reported-writer-settlement'
    return result


def hidhide_writer_observation_evidence(members,current_pid):
    # Three fixed banks prevent older writers exhausting current-PID witnesses.
    relations=('current-pid-match','other-or-uncompared','unknown-writer-tuple')
    states=('unsupported-event','unsupported-scope','unsupported-phase-or-reason','invalid-fields',
        'reported-writer-settlement','reported-deferred','reported-launch-rejected','reported-history-hint')
    banks={key:{'rows':0,'first':[],'last':collections.deque(maxlen=4),'witnesses':{},'states':collections.Counter()} for key in relations}
    current=str(current_pid) if type(current_pid) is int and 0<current_pid<(1<<32) else current_pid if type(current_pid) is str and 0<len(current_pid)<=10 and current_pid.isascii() and current_pid.isdecimal() and 0<int(current_pid)<(1<<32) else None
    total=0
    for filename,rows,metadata in members:
        if type(filename) is not str or not re.fullmatch(r'native-(?:lifecycle|detail)\.log(?:\.(?:prev|previous|[0-9]+))?',filename):continue
        for line,row in rows:
            if not isinstance(row,dict) or row.get('event')!='hidhide-cli-writer':continue
            total+=1;p=row.get('pid');b=row.get('writerProcessCreation100ns')
            pk=type(p) is int and 0<p<(1<<32);bk=type(b) is str and 0<len(b)<=20 and b[0]!='0' and b.isascii() and b.isdecimal() and int(b)<(1<<64)
            relation='unknown-writer-tuple' if not(pk and bk) else 'current-pid-match' if current is not None and str(p)==current else 'other-or-uncompared';bank=banks[relation];bank['rows']+=1
            record=hidhide_writer_observation_record(row);state=record['state'];assert state in states;bank['states'][state]+=1
            item={'encounterOrdinal':total,'file':filename[:160],'line':line if type(line) is int and 0<line<(1<<64) else None,'pidRelation':relation,'writerPid':p if pk else None,'writerProcessCreation100ns':b if bk else None,'writerIdentityProven':False,'record':record}
            when=row.get('time')
            if type(when) is str and len(when)<=64:item['time']=when
            for key in ('archiveEntryIndex','archiveEntryOccurrence'):
                value=metadata.get(key) if isinstance(metadata,dict) else None
                if type(value) is int and 0<=value<(1<<64):item[key]=value
            if len(bank['first'])<4:bank['first'].append(item)
            else:bank['last'].append(item)
            bank['witnesses'].setdefault(('state',state),item)
            reason=record['reported'].get('reason','other-or-unavailable');bank['witnesses'].setdefault(('reason',reason),item)
    selected={item['encounterOrdinal']:item for bank in banks.values() for item in bank['first']+list(bank['last'])+list(bank['witnesses'].values())}
    return {'schemaVersion':1,'scope':'retained-same-row-HidHide-CLI-writer-observations;not-device-or-game-results',
        'rowsObserved':total,'relations':[{'pidRelation':key,'rowsObserved':banks[key]['rows'],'rowsByState':dict(banks[key]['states'])} for key in relations],
        'observations':[selected[k] for k in sorted(selected)],'maxListedObservations':78,'rowsNotListed':total-len(selected),
        'selection':'per fixed3 PID-relation banks:first4,last4,first8 states,first10 fixed reason classes',
        'uniqueCommandsCounted':False,'rowsJoined':False,'completeHistoryProven':False,'physicalReturnProven':False,
        'virtualTargetRetiredProven':False,'externalGameInputProven':False,'liveProcessOrJobStateProven':False,
        'readingRules':['settled is the original CLI-writer report only;it is not device-return or virtual-endpoint proof',
            'same-PID relation is a manifest comparison;birth is preserved per row and current lifetime is unproven',
            'parent and child identities belong to this row;writer identity never comes from child or parent',
            'deferred/launch-rejected and unknown contracts are retained as observations;missing error codes are not zero',
            'raw rows and duplicate file occurrences remain separate;equal phase/reason does not authorize deletion']}


def hidhide_output_bytes_record(row):
    """Receipt-buffer evidence only; never guess a code page or device result."""
    value=row.get('outputBytes')
    state=('missing' if 'outputBytes' not in row else 'null' if value is None else
           'object' if isinstance(value,dict) else 'invalid-type')
    result={'state':state,'reported':{},'fieldIssues':[],'decodedCodePage':None,
            'completeChildOutputProven':False,'physicalReturnProven':False}
    if state!='object':return result
    if (type(value.get('schema')) is not int or value['schema']!=1 or
            value.get('scope')!='failure-receipt-buffer-prefix;not-complete-child-output'):
        result['state']='unsupported';return result
    def uint(v):return type(v) is int and 0<=v<(1<<64)
    specs=(('schema',lambda v:type(v) is int and v==1),('encoding',lambda v:v=='hex-lowercase'),
        ('bufferObserved',lambda v:type(v) is bool),
        ('bufferBytes',lambda v:v is None or uint(v)),
        ('retainedBytes',lambda v:v is None or (uint(v) and v<=2048)),
        ('maxRetainedBytes',lambda v:type(v) is int and v==2048),
        ('truncated',lambda v:v is None or type(v) is bool),
        ('data',lambda v:v is None or (isinstance(v,str) and len(v)<=4096 and re.fullmatch('[0-9a-f]*',v) is not None)),
        ('codePageKnown',lambda v:v is False),
        ('scope',lambda v:v=='failure-receipt-buffer-prefix;not-complete-child-output'))
    fields=result['reported'];issues=result['fieldIssues']
    for key,valid in specs:
        if key not in value:issues.append(key+':missing')
        elif not valid(value[key]):issues.append(key+':invalid-type-range-or-value')
        else:fields[key]=value[key]
    if issues:result['state']='invalid-fields';return result
    keys=('bufferBytes','retainedBytes','truncated','data')
    if not fields['bufferObserved']:
        if any(fields[k] is not None for k in keys):issues.append('unobserved-buffer:nonnull-values')
        result['state']='contradictory' if issues else 'reported-buffer-unknown';return result
    if any(fields[k] is None for k in keys):issues.append('observed-buffer:null-values')
    else:
        if fields['retainedBytes']!=min(fields['bufferBytes'],2048):issues.append('retainedBytes:prefix-length-conflict')
        if len(fields['data'])!=2*fields['retainedBytes']:issues.append('data:hex-length-conflict')
        if fields['truncated']!=(fields['bufferBytes']>2048):issues.append('truncated:buffer-length-conflict')
        if type(row.get('outputTruncated')) is bool and row['outputTruncated']!=fields['truncated']:
            issues.append('outputTruncated:wrapper-conflict')
    result['state']='contradictory' if issues else ('reported-buffer-prefix' if fields['truncated'] else 'reported-whole-receipt-buffer')
    return result


def hidhide_output_bytes_evidence(members,current_pid):
    head,tail,witnesses=[],collections.deque(maxlen=8),{}
    counts=collections.Counter();total=0
    stages=('admission','cli-path','pipe-create','pipe-inheritance','process-create',
            'capture-or-wait','exit-query','child-exit','writer-settlement')
    for filename,rows,metadata in members:
        if not re.fullmatch(r'native-(?:lifecycle|detail)(?:\.log(?:\.(?:prev|previous|\d+))?|\.(?:prev|previous|\d+)\.log)',filename):continue
        for line,row in rows:
            if row.get('event')!='hidhide.command-failed':continue
            total+=1;raw=hidhide_output_bytes_record(row);counts[raw['state']]+=1
            ref=(total,filename,line,row,metadata,raw)
            if len(head)<8:head.append(ref)
            else:tail.append(ref)
            witnesses.setdefault(('state',raw['state']),ref)
            stage=row.get('stage')
            if isinstance(stage,str) and stage in stages:witnesses.setdefault(('stage',stage),ref)
    selected=sorted({v[0]:v for v in head+list(tail)+list(witnesses.values())}.values(),key=lambda v:v[0])
    observations=[]
    for ordinal,filename,line,row,metadata,raw in selected:
        writer,role=process_identity(row,filename);reported={};issues=[]
        for key in ('operation','stage','error','childExitCode','failureReceipt','failureReceiptLimit','outputEncoding','physicalReturnObserved','outputTruncated'):
            if key not in row:continue
            value=row[key]
            valid=(type(value) is bool if key=='outputTruncated' else
                   value is None or (type(value) is int and 0<=value<(1<<32)) if key=='childExitCode' else
                   type(value) is int and 0<=value<(1<<32) if key in ('error','failureReceipt','failureReceiptLimit') else
                   isinstance(value,str) and len(value)<=256)
            if valid:reported[key]=value
            else:issues.append(key+':invalid-type-range-or-length')
        text=row.get('output');textView={'state':'missing' if 'output' not in row else 'invalid-type'}
        if isinstance(text,str):textView={'state':'recorded','text':text[:512],'characters':len(text),'summaryTruncated':len(text)>512}
        birth=row.get('writerProcessCreation100ns')
        birthKnown=isinstance(birth,str) and 0<len(birth)<=20 and birth.isascii() and birth.isdecimal() and 0<int(birth)<(1<<64)
        time=stamp(row,filename)
        observations.append({'file':filename,'line':line,**metadata,'encounterOrdinal':ordinal,
            'event':'hidhide.command-failed','writerPid':None if writer=='unknown' else writer,'writerIdentityField':role,
            'writerProcessCreation100ns':birth if birthKnown else None,
            'currentPidMatch':None if writer=='unknown' or current_pid is None else writer==str(current_pid),
            'currentLifetimeProven':False,'time':time if time is None or len(time)<=512 else None,
            'reported':reported,'fieldIssues':issues,'outputText':textView,'outputBytes':raw})
    return {'schemaVersion':1,'scope':'retained-native-HidHide-failure-receipt-buffer-evidence',
        'receiptRowsObserved':total,'countsByByteState':dict(counts),'observations':observations,
        'maxListedObservations':34,'rowsNotListed':total-len(observations),
        'selection':'first8,last8,first9-fixed-byte-states-and-first9-known-stages',
        'rawSampleMaxBytes':2048,'encoding':'hex-lowercase;codepage-unknown',
        'rowsJoined':False,'uniqueFailuresOrCommandsCounted':False,'childExitAndWrapperErrorMerged':False,
        'completeChildOutputProven':False,'physicalReturnProven':False,
        'readingRules':['hex preserves only the observed receipt-buffer prefix;unknown buffer is not empty output',
            'display text may replace invalid UTF8;original byte encoding/codepage is not inferred',
            'error is the original wrapper report;childExitCode is a separate child result,not necessarily a Win32 device code',
            '32-per-power receipt admission does not count complete failures;old missing bytes remain unavailable',
            'rotations and duplicate ZIP occurrences remain separate;no PID/birth or operation pairing across rows']}


def export_operation_record(carrier):
    """Validate this carrier only; a token never authenticates or pairs exports."""
    carrier = carrier if isinstance(carrier, dict) else {}
    value = carrier.get('exportOperation')
    state = ('missing' if 'exportOperation' not in carrier else 'null' if value is None else
             'object' if isinstance(value, dict) else 'invalid-type')
    result = {'state': state, 'reported': {}, 'fieldIssues': [],
              'operationRowsJoined': False, 'currentExportOrIpcDeliveryProven': False}
    if state != 'object':return result
    if type(value.get('schema')) is not int or value['schema'] != 1:
        result['state'] = 'unsupported';return result
    def decimal(v, positive=False):
        return (isinstance(v, str) and 0 < len(v) <= 20 and v.isascii() and v.isdecimal() and
                str(int(v)) == v and (0 < int(v) if positive else 0 <= int(v)) and int(v) < (1 << 64))
    specs = (
        ('schema', lambda v: type(v) is int and v == 1),
        ('id', lambda v: isinstance(v, str) and 0 < len(v) <= 80),
        ('writerPid', lambda v: type(v) is int and 0 < v < (1 << 32)),
        ('writerProcessCreation100ns', lambda v: v is None or decimal(v, True)),
        ('ordinal', lambda v: decimal(v, True)),
        ('startedTickMs', decimal),
        ('scope', lambda v: v == 'admitted-ordinary-log-export;diagnostic-correlation-only'))
    for key, valid in specs:
        if key not in value:result['fieldIssues'].append(key + ':missing')
        elif not valid(value[key]):result['fieldIssues'].append(key + ':invalid-type-range-or-value')
        else:result['reported'][key] = value[key]
    if result['fieldIssues']:
        result['state'] = 'invalid-fields';return result
    fields = result['reported']
    if fields['id'] != f"{fields['writerPid']}-{fields['startedTickMs']}-{fields['ordinal']}":
        result['state'] = 'contradictory';result['fieldIssues'].append('id:component-mismatch');return result
    result['state'] = 'reported-process-operation' if fields['writerProcessCreation100ns'] is not None else 'reported-pid-operation;birth-unknown'
    return result


def export_copy_failure_record(row):
    """Interpret one terminal's bounded wrapper using the original copy validator."""
    field = 'snapshotCopyFailureEvidence'
    value = row.get(field)
    state = ('missing' if field not in row else 'null' if value is None else
             'object' if isinstance(value, dict) else 'invalid-type')
    item = {'state': state, 'reported': {}, 'fieldIssues': [], 'failures': [],
            'failureRowsNotInspected': 0, 'innerCopyStates': []}
    if state != 'object':
        return item
    counters = ('failureObservations', 'evidenceBuildFailures', 'retainedFailures', 'maxRetainedFailures')
    flags = ('retainedObservedFailuresComplete', 'sourcePathsOmitted',
             'sourceStabilityDurabilityOrResourceReturnProven')
    fields = ('schema', 'scope') + counters + flags + ('failures',)
    issues, selected = item['fieldIssues'], item['reported']
    def uint(value, bits=64):
        return type(value) is int and 0 <= value < (1 << bits)
    known = (type(value.get('schema')) is int and value['schema'] == 1 and
             value.get('scope') == 'mandatory-shared-copy-failures-in-this-export-call')
    for key in fields[:-1]:
        if key not in value:
            issues.append(key + ':missing');continue
        part = value[key]
        valid = (type(part) is int and part == 1 if key == 'schema' else
                 isinstance(part, str) and len(part) <= 256 if key == 'scope' else
                 uint(part) if key in counters else type(part) is bool)
        if valid:
            selected[key] = part
        else:
            issues.append(key + ':invalid-type-or-range')
    item['unknownFieldCount'] = len(value) - sum(key in value for key in fields)
    failures = value.get('failures')
    shape = ('missing' if 'failures' not in value else 'null' if failures is None else
             'array' if isinstance(failures, list) else 'invalid-type')
    item['failuresFieldState'] = shape
    if shape in ('missing', 'invalid-type'):
        issues.append('failures:' + shape)
    retained = failures if isinstance(failures, list) else []
    item['failureRowsNotInspected'] = max(0, len(retained) - 2)
    conflict = []
    if len(retained) > 2:
        conflict.append('failure-rows-exceed-original-capacity')
    previous_attempt = None
    for index, failure in enumerate(retained[:2]):
        detail = {'failureRowIndex': index, 'reported': {}, 'fieldIssues': []}
        item['failures'].append(detail)
        if not isinstance(failure, dict):
            detail['fieldIssues'].append('failure-row:invalid-type')
            issues.append('failures[' + str(index) + ']:invalid-row');continue
        for key in ('attempt', 'listIndex', 'entry', 'win32Error'):
            part = failure.get(key)
            valid = (type(part) is int and 1 <= part <= 2 if key == 'attempt' else
                     uint(part) if key == 'listIndex' else uint(part, 32) if key == 'win32Error' else
                     isinstance(part, str) and 0 < len(part) <= 256 and
                         not any(c in part for c in ('/', '\\', ':', '\x00')))
            if key not in failure:
                detail['fieldIssues'].append(key + ':missing')
            elif valid:
                detail['reported'][key] = part
            else:
                detail['fieldIssues'].append(key + ':invalid-type-range-or-basename')
        attempt = detail['reported'].get('attempt')
        if attempt is not None:
            if previous_attempt is not None and attempt <= previous_attempt:
                conflict.append('failure-attempt-order-or-duplicate')
            previous_attempt = attempt
            if type(row.get('attempts')) is int and 0 <= row['attempts'] <= 2 and attempt > row['attempts']:
                conflict.append('failure-attempt-after-reported-terminal-attempt')
            if row.get('ok') is True and type(row.get('attempts')) is int and attempt == row['attempts']:
                conflict.append('success-on-reported-failed-copy-attempt')
        # Only consume the pure validator's inner observation. Synthetic
        # adapter member/occurrence/location must never become archive evidence.
        name = 'log-export-snapshot.json'
        adapted_row = {key: failure[key] for key in ('entry', 'copyObservation') if key in failure}
        adapted = {name: {'files': [adapted_row]}}
        selections = {key: {'status': 'selected' if key == name else 'missing',
                           'selectedOccurrence': None} for key in (name, 'steam/steam-log-export.json')}
        checked = copy_observation_evidence(adapted, selections)['rows'][0]
        detail['copyObservation'] = {key: checked[key] for key in
            ('state', 'reported', 'fieldIssues', 'fieldsNotProjected')}
        for key, _ in SNAPSHOT_WRITER_BOUNDARIES:
            if key in checked:
                detail['copyObservation'][key] = checked[key]
        item['innerCopyStates'].append(checked['state'])
        if detail['fieldIssues']:
            issues.append('failures[' + str(index) + ']:invalid-fields')
        if checked['state'] == 'reported-complete':
            conflict.append('failure-row-reports-completed-copy')
        elif checked['state'] in ('contradictory', 'not-started'):
            conflict.append('failure-row-copy-progress-inconsistent')
        elif checked['state'] != 'reported-incomplete':
            issues.append('failures[' + str(index) + '].copyObservation:' + checked['state'])
    if all(key in selected for key in counters + flags):
        observed, built, count, capacity = (selected[key] for key in counters)
        complete = selected['retainedObservedFailuresComplete']
        if capacity != 2:
            conflict.append('unsupported-retention-capacity')
        if count != len(retained):
            conflict.append('retained-count-disagrees-with-array')
        if count > capacity or count + built > observed:
            conflict.append('failure-coverage-counters-disagree')
        if count < capacity and count + built < observed:
            conflict.append('missing-failure-accounting-before-capacity')
        if complete != (count == observed and built == 0):
            conflict.append('coverage-complete-flag-disagrees')
        if selected['sourcePathsOmitted'] is not True or selected['sourceStabilityDurabilityOrResourceReturnProven'] is not False:
            conflict.append('unsupported-path-or-proof-claim')
        if type(row.get('attempts')) is int and 0 <= row['attempts'] <= 2 and observed > row['attempts']:
            conflict.append('failure-observations-exceed-terminal-attempts')
    issues.extend(conflict)
    item['state'] = ('unsupported' if not known else 'invalid-fields' if issues and not conflict else
        'contradictory' if conflict else 'reported-no-observed-mandatory-copy-failures'
        if selected['failureObservations'] == 0 else 'reported-complete-observed-failure-coverage'
        if selected['retainedObservedFailuresComplete'] else 'reported-incomplete-observed-failure-coverage')
    return item


EXPORT_TIMING_PHASES = ('flush','steamDiscovery','cachedStateAndFileDiscovery','windowsSystemPowerReport',
                        'remainingStateAndManifest','windowsReportAndRemainingState','manifestWrite',
                        'availabilityAndDestination','snapshotIndexAndArchive')


HAPTIC_FAILURE_REASONS=('source-pending','power-transition','manual-pulse-active','source-changed','open-failed',
    'write-failed','io-pending','cancelled','queue-expired','throttled','queue-full','pulse-stop-unavailable')


def haptic_failure_attempt_record(row):
    result={'state':'missing','reported':{},'reasonCounts':{},'lastErrorCodes':{},'fieldIssues':[],
            'physicalFailureInferred':False,'outputApiFailureInferred':False,'droppedAddedToFailed':False}
    issues=result['fieldIssues']
    for key in ('sent','failed','dropped'):
        value=row.get(key)
        if type(value) is int and 0<=value<(1<<64):result['reported'][key]=value
        else:issues.append(key+':missing-or-invalid-type-range')
    for field,target,limit in (('reasons','reasonCounts',64),('errors','lastErrorCodes',32)):
        values=row.get(field)
        if values is None:continue
        if not isinstance(values,dict):issues.append(field+':invalid-type');continue
        result[field+'UnprojectedEntries']=sum(key not in HAPTIC_FAILURE_REASONS for key in values)
        for reason in HAPTIC_FAILURE_REASONS:
            if reason not in values:continue
            count=values[reason]
            if type(count) is int and 0<=count<(1<<limit):result[target][reason]=count
            else:issues.append(field+'.'+reason+':invalid-type-range')
    if 'failureAttemptEvidence' not in row:return result
    value=row['failureAttemptEvidence']
    if not isinstance(value,dict):result['state']='null' if value is None else 'invalid-fields';return result
    if type(value.get('schema')) is not int or value['schema']!=1:
        result['state']='unsupported';return result
    expected='unsuccessful-receipts-original-output-attempt-flag;excludes-dropped'
    if value.get('scope')!=expected:issues.append('scope:unsupported')
    for key in ('attempted','notAttempted','unknown'):
        count=value.get(key)
        if type(count) is int and 0<=count<(1<<64):result['reported'][key]=count
        else:issues.append(key+':missing-or-invalid-type-range')
    for key in ('outputApiFailureProven','physicalFailureProven'):
        if value.get(key) is not False:issues.append(key+':unsupported-proof-claim')
    fields=result['reported']
    if all(key in fields for key in ('failed','attempted','notAttempted','unknown')):
        if fields['attempted']+fields['notAttempted']+fields['unknown']!=fields['failed']:
            issues.append('attempt-partition-disagrees-with-failed')
    if issues:result['state']='invalid-fields';return result
    present=[key for key in ('attempted','notAttempted','unknown') if fields[key]]
    result['state']=('reported-no-unsuccessful' if not present else 'reported-mixed-attempt-states' if len(present)>1 else
        {'attempted':'reported-attempted-only','notAttempted':'reported-not-attempted-only','unknown':'reported-unknown-only'}[present[0]])
    return result


def touch_haptic_preparation_evidence(availability,selection,current_pid):
    """Four fixed source observations;not a native file or motor failure count."""
    view={'state':'receipt-unavailable','selectionStatus':selection.get('status'),'reported':{},'observations':{},'fieldIssues':[],
          'currentDeviceOrFileHealthProven':False,'lostRowsOrPendingSummaryInferred':False,'recoveryScheduledInferred':False}
    if selection.get('status')!='selected' or not isinstance(availability,dict):return view
    if 'touchHapticSummaryPreparationEvidence' not in availability:view['state']='missing';return view
    wrapper=availability['touchHapticSummaryPreparationEvidence']
    if not isinstance(wrapper,dict):view['state']='null' if wrapper is None else 'invalid-type';return view
    stats=wrapper.get('stats');issues=view['fieldIssues']
    pid=wrapper.get('writerPid');birth=wrapper.get('writerProcessCreation100ns')
    if type(pid) is int and 0<pid<=0xffffffff:view['reported']['writerPid']=pid;view['currentPidMatch']=None if current_pid is None else str(pid)==str(current_pid)
    else:issues.append('writerPid:invalid-or-missing')
    if 'writerProcessCreation100ns' not in wrapper:issues.append('writerProcessCreation100ns:missing')
    elif birth is None:view['reported']['writerProcessCreation100ns']=None
    elif isinstance(birth,str) and birth.isascii() and birth.isdecimal() and 0<len(birth)<=20 and 0<int(birth)<(1<<64):view['reported']['writerProcessCreation100ns']=birth
    else:issues.append('writerProcessCreation100ns:invalid')
    if not isinstance(stats,dict):view['state']='stats-null' if stats is None else 'stats-invalid-type';return view
    schema=stats.get('schema')
    if type(schema) is not int or schema not in (1,2):view['state']='unsupported-schema';return view
    view['reported']['schema']=schema;view['exactQueuedTaskCancellationCountProven']=False
    scope='current-Native-touch-summary-original-submission-and-materialization-observations' if schema==1 else 'current-Native-touch-summary-original-submission-materialization-and-queue-clear-observations'
    required={'scope':scope,'countsAreUniqueRows':False,
              'countsAreLostRows':False,'snapshotAtomicAcrossCounters':False,'pendingPlainSummaryRead':False,
              'nativeFileDeliveryProven':False,'automaticRetryScheduled':False,'counterLimit':(1<<64)-1}
    for key,wanted in required.items():
        if type(stats.get(key)) is not type(wanted) or stats.get(key)!=wanted:issues.append(key+':missing-or-unsupported')
        else:view['reported'][key]=wanted
    counts=stats.get('observations');names=('submitRefused','submitException','materializationOrOfferException','attemptEvidenceOmission')
    if schema==2:names+=('queueClearPendingObserved',)
    if not isinstance(counts,dict):issues.append('observations:invalid-or-missing')
    else:
        for key in names:
            v=counts.get(key)
            if type(v) is int and 0<=v<(1<<64):view['observations'][key]=v
            else:issues.append(key+':invalid-or-missing')
        view['unprojectedObservationCount']=len(counts)-sum(key in counts for key in names)
    saturated=stats.get('counterSaturated')
    if type(saturated) is not bool:issues.append('counterSaturated:missing-or-invalid')
    elif len(view['observations'])==len(names) and saturated!=any(v==(1<<64)-1 for v in view['observations'].values()):issues.append('counterSaturated:contradictory')
    else:view['reported']['counterSaturated']=saturated
    view['state']='invalid-fields' if issues else 'reported-source-observations'
    view['readingRules']=['submitRefused is original admission false,not a device or file error',
        'an exception or omitted optional partition can be observed more than once for the same retained counters;do not sum as lost rows',
        'counts remain until an original end submits/materializes again;no automatic retry,thread,timer or delivery acknowledgement is added',
        'writer identity is reported only;no live PID/birth or source-PDB authority is inferred',
        'schema2 queueClearPendingObserved counts original clear sites seeing a pending latch;waiting submitters and claimed callbacks can also be counted;not exact task cancellations']
    return view


def haptic_failure_attempt_evidence(members,current_pid):
    first,last,witnesses=[],collections.deque(maxlen=16),{};counts=collections.Counter();seen=0
    for filename,rows,metadata in members:
        if not re.fullmatch(r'native-(?:lifecycle|detail)\.log(?:\.(?:prev|previous|\d+))?',filename):continue
        for line,row in rows:
            if row.get('event')!='gamepad-touch-haptics-summary':continue
            seen+=1;view=haptic_failure_attempt_record(row);counts[view['state']]+=1
            writer,field=process_identity(row,filename);birth=row.get('writerProcessCreation100ns')
            birthKnown=isinstance(birth,str) and 0<len(birth)<=20 and birth.isascii() and birth.isdecimal() and 0<int(birth)<(1<<64)
            observedTime=stamp(row,filename)
            item={'file':filename,'line':line,**metadata,'writerPid':None if writer=='unknown' else writer,
                'writerIdentityField':field,'writerProcessCreation100ns':birth if birthKnown else None,
                'writerLifetimeKnown':writer!='unknown' and birthKnown,
                'currentPidMatch':None if writer=='unknown' or current_pid is None else writer==str(current_pid),
                'time':observedTime if observedTime is None or len(observedTime)<=512 else None,'evidence':view}
            ref=(seen,item)
            if len(first)<16:first.append(ref)
            last.append(ref);witnesses.setdefault(view['state'],ref)
    listed=dict(first+list(last)+list(witnesses.values()))
    return {'schemaVersion':1,'scope':'retained-touch-feedback-summary-receipts;not-current-action-or-hardware-failure-proof',
        'rowsObserved':seen,'countsByState':dict(counts),'rows':[listed[key] for key in sorted(listed)],
        'maxListedRows':41,'rowsNotListed':seen-len(listed),'oldAttemptFlagsInferredFromReason':False,
        'summaryCountsSummedAcrossWindowsOrWriters':False,'errorsAreLastOriginalCodesNotCounts':True}


def export_phase_timing_record(row, field='phaseElapsedMs'):
    result={'state':'missing','elapsedMs':{},'fieldIssues':[],'unrecognisedPhaseCount':0,
            'scope':'completed-phase-observations-only;missing-is-unknown;not-complete-export-or-CPU-proof'}
    if field not in row:return result
    value=row[field]
    if not isinstance(value,dict):
        result['state']='null' if value is None else 'invalid-type';return result
    for phase in EXPORT_TIMING_PHASES:
        if phase not in value:continue
        number=value[phase]
        if type(number) is int and 0<=number<(1<<64):result['elapsedMs'][phase]=number
        else:result['fieldIssues'].append(phase+':invalid-type-or-range')
    result['unrecognisedPhaseCount']=sum(key not in EXPORT_TIMING_PHASES for key in value)
    result['state']='invalid-fields' if result['fieldIssues'] else 'reported-completed-phases'
    return result


def windows_sleep_scheduling_record(availability, phases):
    result=dict(state='missing', reported={}, fieldIssues=[])
    if 'windowsSleepScheduling' not in availability:return result
    row=availability['windowsSleepScheduling']
    if not isinstance(row,dict):
        result['state']='null' if row is None else 'invalid-type';return result
    if type(row.get('schema')) is not int or row['schema']!=1 or row.get('scope')!='export-wall-intervals;may-overlap':
        result['state']='unsupported';return result
    fields=result['reported'];issues=result['fieldIssues']
    for key in ('reportAdapterMs','remainingStateMs','overlapMs','joinMs'):
        value=row.get(key)
        if type(value) is int and 0<=value<(1<<64):fields[key]=value
        else:issues.append(key+':missing-invalid-type-or-range')
    mode=row.get('mode')
    if mode in ('overlapped-task','inline-launch-fallback'):fields['mode']=mode
    else:issues.append('mode:missing-or-unsupported')
    if not issues:
        report,remaining,overlap,join=(fields[k] for k in ('reportAdapterMs','remainingStateMs','overlapMs','joinMs'))
        if overlap>min(report,remaining) or (mode=='inline-launch-fallback' and overlap):
            issues.append('overlapMs:inconsistent-with-intervals')
        combined=phases.get('windowsReportAndRemainingState')
        if combined is not None and max(report+remaining-overlap,remaining+join)>combined:
            issues.append('windowsReportAndRemainingState:shorter-than-contained-intervals')
        if 'manifestNativeIdentity' in row:
            identity=row['manifestNativeIdentity']
            if not isinstance(identity,dict) or not all(
                    type(identity.get(k)) is int and 0<=identity[k]<(1<<64) for k in ('elapsedMs','overlapMs')):
                issues.append('manifestNativeIdentity:invalid-type-or-range')
            else:
                elapsed,shared=identity['elapsedMs'],identity['overlapMs']
                if mode!='overlapped-task' or not (shared<=min(elapsed,overlap) and elapsed-shared<=remaining-overlap):
                    issues.append('manifestNativeIdentity:inconsistent-with-remaining-state')
                else:
                    fields['manifestNativeIdentity']=dict(elapsedMs=elapsed,overlapMs=shared)
    result['state']='invalid-fields' if issues else 'reported-overlap-intervals'
    return result


def windows_query_capture_finalization_record(value):
    """Original current-query API returns only,without resource-retirement inference."""
    result={'state':'missing','reported':{},'fieldIssues':[],'rootWaitState':'unknown',
            'allResourcesReturnedProven':False,'subtreeRetiredProven':False,'queryOrSleepSuccessInferred':False}
    if 'commandCaptureFinalization' not in value:return result
    row=value['commandCaptureFinalization']
    if not isinstance(row,dict):result['state']='null' if row is None else 'invalid-type';return result
    scope='original-post-capture-job-termination-and-root-wait;not-all-resources-or-subtree-retirement-proof'
    if type(row.get('schema')) is not int or row['schema']!=1 or row.get('scope')!=scope:result['state']='unsupported';return result
    def dword(v):return type(v) is int and 0<=v<(1<<32)
    specs=(('schema',lambda v:type(v) is int and v==1),('scope',lambda v:v==scope),
        ('entered',lambda v:type(v) is bool),('rootWaitAllowanceMs',lambda v:type(v) is int and v==1000),
        ('jobTerminationAccepted',lambda v:v is None or type(v) is bool),
        ('jobTerminationError',lambda v:v is None or dword(v)),
        ('rootWaitResult',lambda v:v is None or dword(v)),('rootWaitError',lambda v:v is None or dword(v)))
    fields=result['reported'];issues=result['fieldIssues']
    for key,valid in specs:
        if key not in row:issues.append(key+':missing')
        elif not valid(row[key]):issues.append(key+':invalid-type-or-range')
        else:fields[key]=row[key]
    timing=('jobTerminationElapsedMs','rootWaitElapsedMs')
    if any(key in row for key in timing):
        for key in timing:
            if key not in row:issues.append(key+':missing')
            elif row[key] is not None and not (type(row[key]) is int and 0<=row[key]<(1<<64)):
                issues.append(key+':invalid-type-or-range')
            else:fields[key]=row[key]
    if issues:result['state']='invalid-fields';return result
    observed=('jobTerminationAccepted','jobTerminationError','rootWaitResult','rootWaitError')
    if not fields['entered']:
        if any(fields[k] is not None for k in observed):issues.append('unentered:nonnull-api-results')
        if any(fields.get(k) is not None for k in timing):issues.append('unentered:nonnull-elapsed-ms')
        result['state']='contradictory' if issues else 'reported-finalization-not-entered';return result
    if any(fields[k] is None for k in observed):issues.append('entered:null-api-results')
    else:
        if fields['jobTerminationAccepted'] and fields['jobTerminationError']!=0:issues.append('accepted-termination:nonzero-error')
        wait=fields['rootWaitResult']
        if wait not in (0,258,0xffffffff):issues.append('rootWaitResult:unsupported-process-wait-result')
        if wait!=0xffffffff and fields['rootWaitError']!=0:issues.append('nonfailed-wait:nonzero-error')
    query=value.get('query')
    if value.get('querySuppressed') is True or type(value.get('queryAttempts')) is not int or value['queryAttempts']!=1 or not isinstance(query,dict) or query.get('ran') is not True:
        issues.append('query:current-started-query-not-proven')
    if timing[0] in fields:
        if any(fields[k] is None for k in timing):issues.append('entered:null-elapsed-ms')
        elif isinstance(query,dict) and type(query.get('elapsedMs')) is int and query['elapsedMs']>=0 and sum(fields[k] for k in timing)>query['elapsedMs']:
            issues.append('elapsed-ms:exceeds-current-query-wall')
    if issues:result['state']='contradictory';return result
    result['state']='reported-post-capture-api-returns'
    result['rootWaitState']={0:'root-signalled',258:'root-not-signalled-in-original-wait',0xffffffff:'root-wait-failed'}[fields['rootWaitResult']]
    return result


def windows_query_failure_output_record(value):
    """Current failed query buffer only; no codepage,complete stream or OS cause."""
    result={'state':'missing','reported':{},'fieldIssues':[],'decodedCodePage':None,
            'completeChildOutputProven':False,'windowsRootCauseProven':False,'sleepSuccessProven':False}
    if 'commandFailureOutputBytes' not in value:return result
    item=value['commandFailureOutputBytes']
    if not isinstance(item,dict):result['state']='null' if item is None else 'invalid-type';return result
    if type(item.get('schema')) is not int or item['schema']!=1 or item.get('scope')!='current-failed-query-returned-buffer-prefix;not-complete-child-output':
        result['state']='unsupported';return result
    def uint(v):return type(v) is int and 0<=v<(1<<64)
    specs=(('schema',lambda v:type(v) is int and v==1),('encoding',lambda v:v=='hex-lowercase'),
        ('bufferObserved',lambda v:type(v) is bool),('bufferBytes',lambda v:v is None or uint(v)),
        ('retainedBytes',lambda v:v is None or uint(v) and v<=2048),
        ('maxRetainedBytes',lambda v:type(v) is int and v==2048),
        ('truncated',lambda v:v is None or type(v) is bool),
        ('data',lambda v:v is None or isinstance(v,str) and len(v)<=4096 and re.fullmatch('[0-9a-f]*',v) is not None),
        ('codePageKnown',lambda v:v is False),
        ('scope',lambda v:v=='current-failed-query-returned-buffer-prefix;not-complete-child-output'))
    fields=result['reported'];issues=result['fieldIssues']
    for key,valid in specs:
        if key not in item:issues.append(key+':missing')
        elif not valid(item[key]):issues.append(key+':invalid-type-range-or-value')
        else:fields[key]=item[key]
    if issues:result['state']='invalid-fields';return result
    keys=('bufferBytes','retainedBytes','truncated','data')
    if not fields['bufferObserved']:
        if any(fields[k] is not None for k in keys):issues.append('unobserved-buffer:nonnull-values')
        result['state']='contradictory' if issues else 'reported-current-failure-buffer-unobserved';return result
    if any(fields[k] is None for k in keys):issues.append('observed-buffer:null-values')
    else:
        if fields['retainedBytes']!=min(fields['bufferBytes'],2048):issues.append('retainedBytes:prefix-length-conflict')
        if len(fields['data'])!=2*fields['retainedBytes']:issues.append('data:hex-length-conflict')
        if fields['truncated']!=(fields['bufferBytes']>2048):issues.append('truncated:buffer-length-conflict')
        if type(value.get('commandOutputBytes')) is int and value['commandOutputBytes']!=fields['bufferBytes']:
            issues.append('commandOutputBytes:buffer-length-conflict')
    query=value.get('query')
    failed=isinstance(query,dict) and type(query.get('ran')) is bool and type(query.get('exitCode')) is int and 0<=query['exitCode']<(1<<32) and type(query.get('win32Error')) is int and 0<=query['win32Error']<(1<<32) and isinstance(query.get('completion'),str) and len(query['completion'])<=64 and (not query['ran'] or query['exitCode']!=0 or query['win32Error']!=0 or query['completion']!='child-exit')
    if value.get('querySuppressed') is True or type(value.get('queryAttempts')) is not int or value['queryAttempts']!=1 or value.get('reportAvailable') is not False or not failed:
        issues.append('query:current-failure-not-proven')
    result['state']='contradictory' if issues else ('reported-current-failure-buffer-prefix' if fields['truncated'] else 'reported-whole-current-failure-buffer')
    return result


def windows_query_outcome_timing_record(value):
    """Code provenance and bounded reported time domains;never duration from code."""
    result={'scope':'reported-query-completion-and-time-domains;current-and-recent-separate',
        'state':'missing','current':{},'recent':{},'fieldIssues':[],
        'exitCodeIsDuration':False,'win32ErrorIsDuration':False,'deadlineViolationOrTotalExportWaitProven':False,
        'childExitOrResourceRetirementInferredFromDeadline':False,'queryOrSleepSuccessInferred':False}
    if not isinstance(value,dict):result['state']='invalid-type';return result
    issues=result['fieldIssues'];timeout=value.get('timeoutMs')
    budgetKnown=type(timeout) is int and 0<=timeout<(1<<32)
    if 'timeoutMs' in value and not budgetKnown:issues.append('timeoutMs:invalid-type-or-range')
    if not any(key in value for key in ('query','querySuppressed','recentDeadline')):return result
    def observed(document,origin,currentBudget):
        view={'state':'missing','origin':origin,'reported':{},'fieldIssues':[],
            'completionKind':'unknown','exitCodeOrigin':'unknown','reportedChildExitCode':None,
            'reportedCaptureExitCode':None,'queryElapsedMs':None,'configuredCaptureTimeoutMs':None,
            'elapsedMinusConfiguredMs':None,'differenceIsDeadlineViolation':False}
        if document is None:view['state']='null';return view
        if not isinstance(document,dict):view['state']='invalid-type';return view
        fields=view['reported'];bad=view['fieldIssues']
        for key in ('ran','exitCode','win32Error','completion','elapsedMs'):
            if key not in document:bad.append(key+':missing');continue
            item=document[key]
            valid=(type(item) is bool if key=='ran' else isinstance(item,str) and len(item)<=64 if key=='completion' else type(item) is int and 0<=item<(1<<(64 if key=='elapsedMs' else 32)))
            if valid:fields[key]=item
            else:bad.append(key+':invalid-type-or-range')
        if 'elapsedMs' in fields:view['queryElapsedMs']=fields['elapsedMs']
        if currentBudget and budgetKnown:
            view['configuredCaptureTimeoutMs']=timeout
            if 'elapsedMs' in fields:view['elapsedMinusConfiguredMs']=fields['elapsedMs']-timeout
        if bad:view['state']='invalid-fields';return view
        ran=fields['ran'];code=fields['exitCode'];error=fields['win32Error'];completion=fields['completion']
        if completion=='child-exit':
            if not ran or error!=0:bad.append('child-exit:inconsistent-capture-fields')
            else:view.update(completionKind='reported-child-exit',exitCodeOrigin='child-process-exit-code',reportedChildExitCode=code)
        elif completion in ('deadline','wait-failed'):
            if not ran or code!=1460 or (completion=='deadline' and error!=0):bad.append('capture-timeout-or-wait:inconsistent-fields')
            else:view.update(completionKind='reported-capture-deadline' if completion=='deadline' else 'reported-process-wait-failure',exitCodeOrigin='capture-wrapper-sentinel',reportedCaptureExitCode=code)
        elif completion in ('canceled','canceled-before-start','canceled-before-resume'):
            if code!=1223 or ran!=(completion=='canceled'):bad.append('capture-cancel:inconsistent-fields')
            else:view.update(completionKind='reported-capture-cancellation',exitCodeOrigin='capture-wrapper-sentinel',reportedCaptureExitCode=code)
        elif completion=='overflow':
            if not ran or code!=111:bad.append('capture-overflow:inconsistent-fields')
            else:view.update(completionKind='reported-capture-output-overflow',exitCodeOrigin='capture-wrapper-sentinel',reportedCaptureExitCode=code)
        elif completion=='exit-code-failed':
            if not ran or code!=(error if error else 31):bad.append('exit-code-query:inconsistent-fields')
            else:view.update(completionKind='reported-child-exit-code-query-failed',exitCodeOrigin='capture-api-error-or-fallback',reportedCaptureExitCode=code)
        elif completion in ('not-started','setup-create-pipe','setup-create-job','setup-configure-job','setup-create-process','setup-assign-job','setup-resume-thread'):
            if ran or code!=0:bad.append('capture-setup:inconsistent-fields')
            else:view.update(completionKind='reported-not-started' if completion=='not-started' else 'reported-capture-setup-failure',exitCodeOrigin='capture-default-value-not-child-exit',reportedCaptureExitCode=code)
        else:view['state']='unsupported-completion';return view
        view['state']='invalid-fields' if bad else 'reported-code-and-time-domains'
        return view
    if 'query' in value:result['current']=observed(value['query'],'current-query',True)
    previous=value.get('recentDeadline')
    if isinstance(previous,dict) and 'query' in previous:result['recent']=observed(previous['query'],'previous-query-in-cooldown-receipt',False)
    elif 'recentDeadline' in value and not isinstance(previous,dict):result['recent']={'state':'invalid-type'}
    for name in ('current','recent'):
        if result[name].get('state') in ('invalid-type','invalid-fields','null','unsupported-completion'):issues.append(name+':'+result[name]['state'])
    suppressed=value.get('querySuppressed')
    if 'querySuppressed' in value and type(suppressed) is not bool:issues.append('querySuppressed:invalid-type')
    attempts=value.get('queryAttempts')
    if 'query' in value and (type(attempts) is not int or attempts!=1 or suppressed is True):issues.append('current-query:attempt-or-suppression-conflict')
    if suppressed is True:
        if type(attempts) is not int or attempts!=0 or 'query' in value:issues.append('suppressed-query:current-observation-conflict')
        result['current']={'state':'suppressed-current-query-not-executed','configuredCaptureTimeoutMs':timeout if budgetKnown else None,'queryElapsedMs':None,'elapsedMinusConfiguredMs':None,'exitCodeOrigin':'no-current-query'}
    result['state']='invalid-fields' if issues else 'reported-code-and-time-domains'
    result['readingRules']=['only timeoutMs is the configured capture allowance;1460 is a capture code,never 1460 milliseconds or 1.46 seconds',
        'child-exit 1460 is a reported child code;deadline 1460 and wait-failed 1460 are wrapper sentinels with different completions',
        'query.elapsedMs covers adapter setup,capture/finalization,and report handling;the reported difference is arithmetic,not measured overrun attribution',
        'a previous cooldown query has no historical budget field here;do not pair its elapsedMs with the current configured timeout',
        'no child exit,subtree retirement,sleep outcome,total export wait or performance benefit follows from these fields']
    return result


def windows_export_query_record(availability):
    result={'state':'missing','reported':{},'fieldIssues':[],'query':{},'recentDeadline':{},
            'currentReportOrSleepSuccessInferred':False}
    if 'windowsSleepDiagnostics' not in availability:return result
    value=availability['windowsSleepDiagnostics']
    if not isinstance(value,dict):result['state']='invalid-type';return result
    if value.get('reason')=='skipped-by-request' or value.get('status')=='NOT_COLLECTED':
        keys=('status','reason','reportAvailable','queryAttempts')
        reported={k:value.get(k) for k in keys}
        valid=(reported==dict(status='NOT_COLLECTED',reason='skipped-by-request',reportAvailable=False,queryAttempts=0)
            and type(reported['queryAttempts']) is int and reported['reportAvailable'] is False
            and value.get('schema')=='ymcc.sleep-export-diagnostics.v1'
            and not any(k in value for k in ('query','recentDeadline','querySuppressed','reportFile','deadlineRetryPolicy')))
        result.update(state='reported-skipped-by-request' if valid else 'invalid-fields',reported=reported if valid else {})
        if not valid:result['fieldIssues'].append('skip-request:conflicting-or-incomplete-receipt')
        return result
    result['outcomeAndTiming']=windows_query_outcome_timing_record(value)
    result['commandCaptureFinalization']=windows_query_capture_finalization_record(value)
    if result['commandCaptureFinalization']['state'] in ('invalid-type','invalid-fields','contradictory','unsupported'):
        result['fieldIssues'].append('commandCaptureFinalization:'+result['commandCaptureFinalization']['state'])
    result['commandFailureOutputBytes']=windows_query_failure_output_record(value)
    for key in ('commandOutputBytes','commandFailureOutputEvidenceBuildFailures','commandCaptureFinalizationBuildFailures'):
        if key in value:
            item=value[key]
            if type(item) is int and 0<=item<(1<<64):result['reported'][key]=item
            else:result['fieldIssues'].append(key+':invalid-type-or-range')
    if result['commandFailureOutputBytes']['state'] in ('invalid-type','invalid-fields','contradictory','unsupported'):
        result['fieldIssues'].append('commandFailureOutputBytes:'+result['commandFailureOutputBytes']['state'])
    for key in ('status','reason','queryAttempts','reportAvailable','querySuppressed','timeoutMs'):
        if key not in value:continue
        item=value[key]
        valid=(type(item) is bool if key in ('reportAvailable','querySuppressed') else
            type(item) is int and 0<=item<=(1 if key=='queryAttempts' else 0xffffffff) if key in ('queryAttempts','timeoutMs') else
            isinstance(item,str) and len(item)<=256)
        if valid:result['reported'][key]=item
        else:result['fieldIssues'].append(key+':invalid-type-or-range')
    def query_fields(document):
        if not isinstance(document,dict):return {}
        out={}
        for key in ('ran','exitCode','win32Error','completion','elapsedMs'):
            if key not in document:continue
            item=document[key]
            if (type(item) is bool if key=='ran' else isinstance(item,str) and len(item)<=64 if key=='completion' else
                    type(item) is int and 0<=item<(1<<(64 if key=='elapsedMs' else 32))):out[key]=item
            else:result['fieldIssues'].append('query.'+key+':invalid-type-or-range')
        return out
    result['query']=query_fields(value.get('query'))
    result['queryPhaseElapsedMs']={'state':'missing','reported':{}}
    if 'queryPhaseElapsedMs' in value:
        phases=value['queryPhaseElapsedMs']; q=result['query']
        keys=('preparation','captureCall','setupThroughResume','captureLoop','reportFileRead')
        elapsed={k:phases[k] for k in keys if k in phases} if isinstance(phases,dict) else {}
        if (not all(k in elapsed for k in keys[:2]) or
            any(type(ms) is not int or not 0<=ms<(1<<64) for ms in elapsed.values()) or
            (('setupThroughResume' in elapsed)!=('captureLoop' in elapsed))):
            state='invalid-fields'
        elif (type(value.get('queryAttempts')) is not int or value['queryAttempts']!=1 or
              value.get('querySuppressed') is True or 'elapsedMs' not in q or
              elapsed['preparation']+elapsed['captureCall']+elapsed.get('reportFileRead',0)>q['elapsedMs'] or
              elapsed.get('setupThroughResume',0)+elapsed.get('captureLoop',0)>elapsed['captureCall'] or
              ('captureLoop' in elapsed and q.get('ran') is not True) or
              ('reportFileRead' in elapsed and not (q.get('ran') is True and
                  q.get('completion')=='child-exit' and q.get('exitCode')==0))):
            state='contradictory'
        else:state='reported-completed-phases'
        if state!='reported-completed-phases':result['fieldIssues'].append('queryPhaseElapsedMs:'+state)
        result['queryPhaseElapsedMs']={'state':state,'reported':elapsed if state=='reported-completed-phases' else {}}
    result['commandCapturePipe']={'state':'missing','reported':{}}
    if 'commandCapturePipe' in value:
        pipe=value['commandCapturePipe']; q=result['query']
        flags=('emptyPeekObserved','nonemptyPeekObserved','emptyReadObserved','bytesReadObserved')
        errors=('firstPeekError','firstReadError')
        fields=('schema','scope',*flags,*errors)
        reported={k:pipe[k] for k in fields if k in pipe} if isinstance(pipe,dict) else {}
        if (len(reported)!=len(fields) or type(reported['schema']) is not int or reported['schema']!=1 or
            reported['scope']!='current-failed-query-capture-loop-before-finalization' or
            any(type(reported[k]) is not bool for k in flags) or
            any(reported[k] is not None and (type(reported[k]) is not int or
                not 0<=reported[k]<(1<<32)) for k in errors)):
            state='invalid-fields'
        elif (type(value.get('queryAttempts')) is not int or value['queryAttempts']!=1 or
              value.get('querySuppressed') is True or q.get('ran') is not True or
              not all(k in q for k in ('completion','exitCode','win32Error')) or
              (q['completion']=='child-exit' and q['exitCode']==0 and q['win32Error']==0) or
              not (reported['emptyPeekObserved'] or reported['nonemptyPeekObserved'] or reported['firstPeekError'] is not None) or
              (not reported['nonemptyPeekObserved'] and
                (reported['emptyReadObserved'] or reported['bytesReadObserved'] or reported['firstReadError'] is not None))):
            state='contradictory'
        else:state='reported-capture-loop-pipe'
        if state!='reported-capture-loop-pipe':result['fieldIssues'].append('commandCapturePipe:'+state)
        result['commandCapturePipe']={'state':state,'reported':reported if state=='reported-capture-loop-pipe' else {}}
    result['deadlineJobAccounting']={'state':'missing','reported':{}}
    if 'deadlineJobAccounting' in value:
        job=value['deadlineJobAccounting']; q=result['query']
        counters=(('totalProcesses',32),('activeProcesses',32),('userTime100ns',63),('kernelTime100ns',63))
        fields=('schema','scope','available','elapsedMs','win32Error',*(k for k,_ in counters))
        reported={k:job[k] for k in fields if k in job} if isinstance(job,dict) else {}
        def uint(item,bits):return type(item) is int and 0<=item<(1<<bits)
        if (len(reported)!=len(fields) or not uint(reported['schema'],32) or reported['schema']!=1 or
            reported['scope']!='owned-command-job-before-deadline-termination' or
            type(reported['available']) is not bool or not uint(reported['elapsedMs'],64) or
            (reported['win32Error'] is not None and not uint(reported['win32Error'],32)) or
            any(reported[k] is not None and not uint(reported[k],bits) for k,bits in counters)):
            state='invalid-fields'
        else:
            available=reported['available']; error=reported['win32Error']
            consistent=(error is None and all(reported[k] is not None for k,_ in counters) and
                        1<=reported['totalProcesses'] and reported['activeProcesses']<=reported['totalProcesses']
                        if available else error is not None and all(reported[k] is None for k,_ in counters))
            current=(type(value.get('queryAttempts')) is int and value['queryAttempts']==1 and
                     value.get('querySuppressed') is not True and q.get('ran') is True and
                     q.get('completion')=='deadline' and q.get('exitCode')==1460 and q.get('win32Error')==0 and
                     value.get('reportAvailable') is False)
            phase=result['queryPhaseElapsedMs']['reported']
            fits=('captureLoop' not in phase or
                  phase['setupThroughResume']+phase['captureLoop']+reported['elapsedMs']<=phase['captureCall'])
            state=('reported-job-accounting' if available else 'reported-query-failure') if consistent and current and fits else 'contradictory'
        if state not in ('reported-job-accounting','reported-query-failure'):
            result['fieldIssues'].append('deadlineJobAccounting:'+state)
            reported={}
        result['deadlineJobAccounting']={'state':state,'reported':reported}
    result['deadlineRootAccounting']={'state':'missing','reported':{}}
    if 'deadlineRootAccounting' in value:
        root=value['deadlineRootAccounting']; q=result['query']
        counters=('userTime100ns','kernelTime100ns')
        keys=('schema','scope','available','elapsedMs','win32Error',*counters)
        reported={key:root[key] for key in keys if key in root} if isinstance(root,dict) else {}
        def uint(item,bits):return type(item) is int and 0<=item<(1<<bits)
        if (len(reported)!=len(keys) or not uint(reported['schema'],32) or reported['schema']!=1 or
            reported['scope']!='owned-command-root-before-deadline-termination' or
            type(reported['available']) is not bool or not uint(reported['elapsedMs'],64) or
            (reported['win32Error'] is not None and not uint(reported['win32Error'],32)) or
            any(reported[key] is not None and not uint(reported[key],64) for key in counters)):
            state='invalid-fields'
        else:
            available=reported['available']; error=reported['win32Error']
            consistent=(error is None and all(reported[key] is not None for key in counters)
                        if available else error is not None and all(reported[key] is None for key in counters))
            current=(type(value.get('queryAttempts')) is int and value['queryAttempts']==1 and
                     value.get('querySuppressed') is not True and value.get('reportAvailable') is False and
                     value.get('reason')=='query-not-completed-successfully' and q.get('ran') is True and
                     q.get('completion')=='deadline' and q.get('exitCode')==1460 and q.get('win32Error')==0)
            phase=result['queryPhaseElapsedMs']['reported']
            fits=('captureLoop' not in phase or
                  phase['setupThroughResume']+phase['captureLoop']+reported['elapsedMs']<=phase['captureCall'])
            state=('reported-root-accounting' if available else 'reported-query-failure') if consistent and current and fits else 'contradictory'
        if state not in ('reported-root-accounting','reported-query-failure'):
            result['fieldIssues'].append('deadlineRootAccounting:'+state)
            reported={}
        result['deadlineRootAccounting']={'state':state,'reported':reported}
    result['deadlineReportPath']={'state':'missing','reported':{}}
    if 'deadlineReportPath' in value:
        observation=value['deadlineReportPath']; q=result['query']
        fields=('schema','scope','state','bytes','win32Error','elapsedMs')
        reported={k:observation[k] for k in fields if k in observation} if isinstance(observation,dict) else {}
        states=('regular-file-metadata','non-regular-path','path-not-found','attribute-query-failed')
        def unsigned(item,bits):return type(item) is int and 0<=item<(1<<bits)
        if (len(reported)!=len(fields) or type(reported['schema']) is not int or reported['schema']!=1 or
            reported['scope']!='owned-report-path-after-current-capture-deadline;metadata-only' or
            reported['state'] not in states or not unsigned(reported['elapsedMs'],64) or
            (reported['bytes'] is not None and not unsigned(reported['bytes'],64)) or
            (reported['win32Error'] is not None and not unsigned(reported['win32Error'],32))):
            state='invalid-fields'
        else:
            outcome=reported['state']; size=reported['bytes']; error=reported['win32Error']
            consistent=(size is not None and error is None if outcome=='regular-file-metadata' else
                        size is None and error is None if outcome=='non-regular-path' else
                        size is None and error in (2,3) if outcome=='path-not-found' else
                        size is None and error is not None and error not in (2,3))
            current=(type(value.get('queryAttempts')) is int and value['queryAttempts']==1 and
                     value.get('querySuppressed') is not True and value.get('reportAvailable') is False and
                     value.get('reason')=='query-not-completed-successfully' and q.get('ran') is True and
                     q.get('completion')=='deadline' and q.get('exitCode')==1460 and q.get('win32Error')==0)
            state='reported-attribute-lookup' if consistent and current else 'contradictory'
        if state!='reported-attribute-lookup':result['fieldIssues'].append('deadlineReportPath:'+state)
        result['deadlineReportPath']={'state':state,'reported':reported if state=='reported-attribute-lookup' else {}}
    def tick_field(item):
        return isinstance(item,str) and item.isascii() and item.isdecimal() and 0<len(item)<=20 and int(item)<(1<<64)
    def exact_int(item, expected):
        return type(item) is int and item==expected
    def cooldown_policy(document, legacy=False):
        if legacy and 'deadlineStreak' not in document and 'streakWindowMs' not in document:
            return 60000
        streak=document.get('deadlineStreak')
        if type(streak) is int and 1<=streak<=3 and exact_int(document.get('streakWindowMs'),300000):
            return 60000 << (streak-1)
        return None
    def deadline(query):
        return (query.get('ran') is True and query.get('completion')=='deadline' and
                query.get('exitCode')==1460 and query.get('win32Error')==0)
    previous=value.get('recentDeadline')
    if isinstance(previous,dict):
        for key in ('originalExportId','powerGeneration','completedTickMs','ageMs','retryAfterMs','cooldownMs','deadlineStreak','streakWindowMs'):
            if key not in previous:continue
            item=previous[key]
            valid=(isinstance(item,str) and len(item)<=128 if key=='originalExportId' else
                tick_field(item)
                if key in ('powerGeneration','completedTickMs') else type(item) is int and 0<=item<=(1<<64)-1)
            if valid:result['recentDeadline'][key]=item
            else:result['fieldIssues'].append('recentDeadline.'+key+':invalid-type-or-range')
        result['recentDeadline']['query']=query_fields(previous.get('query'))
    if value.get('querySuppressed') is True:
        p=result['recentDeadline'];q=p.get('query',{})
        expected_cooldown=cooldown_policy(previous,legacy=True) if isinstance(previous,dict) else None
        coherent=(value.get('queryAttempts')==0 and type(value.get('queryAttempts')) is int and
            value.get('status')=='UNKNOWN' and value.get('reportAvailable') is False and
            value.get('reason')=='recent-query-deadline' and 'query' not in value and
            deadline(q) and
            all(key in p for key in ('ageMs','retryAfterMs','cooldownMs')) and
            p['ageMs']<p['cooldownMs']==expected_cooldown and p['retryAfterMs']==p['cooldownMs']-p['ageMs'])
        if not coherent:result['fieldIssues'].append('recentDeadline:conflicting-reuse-claim')
    if 'deadlineRetryPolicy' in value:
        policy=value['deadlineRetryPolicy']
        keys=('schema','deadlineStreak','streakWindowMs','cooldownMs','powerGeneration','completedTickMs')
        valid=(isinstance(policy,dict) and exact_int(policy.get('schema'),1) and
            exact_int(policy.get('cooldownMs'),cooldown_policy(policy)) and
            tick_field(policy.get('powerGeneration')) and tick_field(policy.get('completedTickMs')) and
            deadline(result['query']) and exact_int(value.get('queryAttempts'),1) and
            value.get('status')=='UNKNOWN' and value.get('reportAvailable') is False and
            value.get('reason')=='query-not-completed-successfully' and 'recentDeadline' not in value and
            value.get('querySuppressed') is not True)
        result['deadlineRetryPolicy']={'state':'reported' if valid else 'invalid',
            'reported':{k:policy[k] for k in keys} if valid else {}}
        if not valid:result['fieldIssues'].append('deadlineRetryPolicy:invalid-or-conflicting')
    result['state']='invalid-fields' if result['fieldIssues'] else 'reported'
    return result


def export_copy_failure_evidence(members, current_pid):
    """Retained terminal rows,not the outcome of the containing ZIP or current IPC."""
    head, tail, witnesses = [], collections.deque(maxlen=16), {}
    counts, outcomes = collections.Counter(), collections.Counter()
    total = 0
    phase_counts,phase_first=collections.Counter(),{}
    for filename, rows, metadata in members:
        if not re.fullmatch(r'native-(?:lifecycle|detail)\.log(?:\.(?:prev|previous|\d+))?', filename):
            continue
        for line, row in rows:
            if row.get('event') != 'log-export-result':
                continue
            total += 1
            evidence = export_copy_failure_record(row)
            outcome = ('reported-success' if row['ok'] else 'reported-failure') if type(row.get('ok')) is bool else (
                'missing' if 'ok' not in row else 'null' if row['ok'] is None else 'invalid-type')
            counts[evidence['state']] += 1;outcomes[outcome] += 1
            phase=row.get('terminalStage')
            body=row.get('bodyReturned')
            known_phases=('before-archive-return','archive-return','body-exception-before-archive','body-exception-after-archive')
            coherent=isinstance(phase,str) and phase in known_phases and type(body) is bool and body==('exception' not in phase)
            phase_counts[phase if coherent else 'missing-or-unrecognised-or-conflicting']+=1
            if coherent and phase not in phase_first:
                phase_first[phase]={'file':filename,'line':line,**metadata,'encounterOrdinal':total,
                    'terminalStage':phase,'bodyReturned':body,'exportOperation':export_operation_record(row),
                    'reported':{k:row[k] for k in ('ok','reason','exceptionClass') if k in row and
                        (type(row[k]) is bool if k=='ok' else isinstance(row[k],str) and len(row[k])<=256)}}
            ref = (total, filename, line, row, metadata, evidence, outcome)
            if len(head) < 16:
                head.append(ref)
            else:
                tail.append(ref)
            witnesses.setdefault(('coverage', evidence['state']), ref)
            witnesses.setdefault(('outcome', outcome), ref)
            for failure in evidence['failures']:
                nested = failure.get('copyObservation', {})
                if nested.get('state') == 'reported-incomplete':
                    witnesses.setdefault(('stage', nested['reported']['stage']), ref)
    # Nine wrapper states,five outcome states,and seven possible failure stages.
    # No map is keyed by basenames,error numbers or unfamiliar strings.
    max_listed = 32 + 9 + 5 + 7
    selected = sorted({ref[0]: ref for ref in head + list(tail) + list(witnesses.values())}.values(),
        key=lambda ref: ref[0])
    observations = []
    for ordinal, filename, line, row, metadata, evidence, outcome in selected:
        writer, writer_field = process_identity(row, filename)
        birth = row.get('writerProcessCreation100ns')
        birth_known = (isinstance(birth, str) and 0 < len(birth) <= 20 and birth.isascii() and
            birth.isdecimal() and 0 < int(birth) < (1 << 64))
        reported, outer_issues = {}, []
        for key in ('ok', 'reason', 'attempts', 'win32Error', 'cleanupOk', 'completion', 'exportScope', 'scope',
                    'terminalStage', 'bodyReturned', 'exceptionClass', 'exportElapsedMs','archiveElapsedMs','phaseTimingBuildFailures',
                    'previousAttemptEvidenceBuildFailures','snapshotAttemptContextEvidenceBuildFailures'):
            if key not in row:
                continue
            part = row[key]
            valid = (type(part) is bool if key in ('ok', 'cleanupOk', 'bodyReturned') else
                     type(part) is int and 0 <= part < (1 << 64) if key in ('exportElapsedMs','archiveElapsedMs','phaseTimingBuildFailures','previousAttemptEvidenceBuildFailures','snapshotAttemptContextEvidenceBuildFailures') else
                     type(part) is int and 0 <= part <= 2 if key == 'attempts' else
                     type(part) is int and 0 <= part < (1 << 32) if key == 'win32Error' else
                     isinstance(part, str) and len(part) <= 256)
            if valid:
                reported[key] = part
            else:
                outer_issues.append(key + ':invalid-type-range-or-length')
        operation=export_operation_record(row)
        fields=operation['reported'];relation='unknown'
        if 'writerPid' in fields and writer!='unknown':
            relation='pid-mismatch' if str(fields['writerPid'])!=writer else 'pid-match;birth-unverified'
            if relation!='pid-mismatch' and birth_known and fields.get('writerProcessCreation100ns') is not None:
                relation='pid-and-birth-match' if fields['writerProcessCreation100ns']==birth else 'birth-mismatch'
        operation['nativeWriterRelation']=relation
        time_value = stamp(row, filename)
        observations.append({'file': filename, 'line': line, **metadata, 'encounterOrdinal': ordinal,
            'event': 'log-export-result', 'writerPid': None if writer == 'unknown' else writer,
            'writerIdentityField': writer_field, 'writerProcessCreation100ns': birth if birth_known else None,
            'writerLifetimeKnown': writer != 'unknown' and birth_known,
            'currentPidMatch': None if writer == 'unknown' or current_pid is None else writer == str(current_pid),
            'currentLifetimeProven': False, 'time': time_value if time_value is None or len(time_value) <= 512 else None,
            'terminalOutcomeState': outcome, 'reported': reported, 'fieldIssues': outer_issues,
            'snapshotCopyFailureEvidence': evidence, 'exportOperation': operation,
            'phaseTimingEvidence':export_phase_timing_record(row),'previousArchiveAttempt':previous_archive_attempt_record(row),
            'archiveAttemptTimingEvidence':archive_attempt_timing_record(row)})
    return {'schemaVersion': 1, 'scope': 'bounded-retained-Native-export-terminals;not-containing-ZIP-or-current-export-outcome',
        'terminalRowsObserved': total, 'countsByCoverageState': dict(counts), 'countsByTerminalOutcome': dict(outcomes),
        'observations': observations, 'maxListedObservations': max_listed, 'rowsNotListed': total - len(observations),
        'selection': 'first16,last16,and-first-fixed-coverage-outcome-or-coherent-failure-stage-witness',
        'bodyStageEvidence':{'countsByCoherentStage':dict(phase_counts),'firstWitnesses':list(phase_first.values()),
            'maxFirstWitnesses':4,'primaryObservationBudgetUnchanged':True,'rowsJoined':False},
        'countUnit': 'retained-terminal-rows;not-unique-exports-files-failures-or-attempts',
        'failureCountsAcrossTerminalsSummed': False, 'sameBasenameOrWriterRowsJoined': False,
        'containingZipOutcomeInferred': False, 'currentExportOutcomeInferred': False,
        'sourceStabilityDurabilityOrResourceReturnProven': False, 'writerBirthBackfilled': False,
        'innerValidation': 'existing pure copy_observation_evidence;synthetic adapter references discarded',
        'readingRules': [
            'terminal is written after snapshot and may first appear in a later export;never attach it to the containing ZIP by position or name',
            'ok=true after retry may retain prior mandatory-copy failure;outer result and historical failure rows remain separate',
            'zero observed failures only covers reached mandatory-copy branches;not all export paths,optional Steam copies or whole-export health',
            'build failures,omitted failures and missing/null/malformed/unsupported wrappers preserve incomplete or unknown coverage',
            'failure rows keep original attempt,listIndex,basename,Win32 code and inner copy progress;zero code does not prove success or zero physical IO',
            'retainedObservedFailuresComplete is only branch-observation coverage;not source stability,durable writes,resource return or currentness',
            'duplicate ZIP occurrences and rotations remain independent;no attempt pairing,unique counts or manifest writer-birth backfill']}


BUFFERED_NATIVE_SINKS = ('gyro-motion.log', 'virtual-gamepad.log', 'focus-trace.log', 'tdp.log', 'tdp-detail.log')
EXTRA_BUFFERED_NATIVE_SINKS = ('gyro-motion-samples.log',)
BUFFERED_NATIVE_FAILURE_STAGES = (
    'append-exception', 'stream-flush', 'stream-flush-exception', 'stream-close',
    'stream-close-exception', 'rotation-size-still-at-limit', 'stream-write',
    'stream-not-ready', 'stream-open-or-tail', 'file-size',
)


def input_capture_queue_evidence(availability, selection, current_pid):
    """Existing queue scalars at export;detached writer work is deliberately unknown."""
    result = {'schemaVersion': 1, 'receipt': 'log-export-availability.json',
        'receiptSelectionStatus': selection['status'], 'archiveEntryOccurrence': selection['selectedOccurrence'],
        'wrapperState': 'receipt-unavailable', 'reported': {}, 'fieldStates': {}, 'fieldIssues': [],
        'consistencyProblems': [], 'queueObservationStatus': 'unknown', 'writerPid': None,
        'currentPidMatch': None, 'writerLifetimeKnown': False, 'writerProcessCreation100ns': None,
        'detachedRowsKnown': False, 'detachedRows': None, 'queueDrainedByExport': False,
        'zeroQueuedRowsProvesWriterIdleOrDelivery': False, 'completeDeliveryProven': False,
        'evictionCounterIsLifetimeHistory': False, 'countsCombinedWithIoFailuresOrPhysicalInput': False,
        'snapshotTickKnown': False, 'snapshotTickMs': None,
        'readingRules': [
            'queuedRows/pendingSignal/evictionsSinceLastDrainOrReset share only the original queue mutex observation',
            'original drain detaches rows and resets the eviction counter before writing;zero queue or evictions does not prove no work or no loss',
            'evictions are original uint64 counters without saturation protection;not lifetime history or physical button counts',
            'export observes only;does not drain,reset or replay queue;not atomic with stream flush or file staging',
            'PID match supplies no birth;no snapshot tick,detached count,device outcome or complete delivery is inferred']}
    if selection['status'] != 'selected':return result
    availability = availability if isinstance(availability, dict) else {}
    key = 'inputCaptureQueueEvidence';value = availability.get(key)
    result['wrapperState'] = ('missing' if key not in availability else 'null' if value is None else
        'object' if isinstance(value, dict) else 'invalid-type')
    if result['wrapperState'] != 'object':return result
    def uint(v):return type(v) is int and 0 <= v < (1 << 64)
    specs = (
        ('schema', lambda v: isinstance(v, str) and 0 < len(v) <= 96),
        ('writerPid', lambda v: type(v) is int and 0 < v < (1 << 32)),
        ('queuedRows', uint), ('capacityRows', lambda v: uint(v) and v > 0),
        ('evictionsSinceLastDrainOrReset', uint), ('pendingSignal', lambda v: type(v) is bool),
        ('writerLifetimeKnown', lambda v: v is False), ('counterSaturationProtected', lambda v: v is False),
        ('evictionCounterIsLifetimeHistory', lambda v: v is False), ('detachedRowsKnown', lambda v: v is False),
        ('detachedRows', lambda v: v is None), ('snapshotTickKnown', lambda v: v is False), ('snapshotTickMs', lambda v: v is None),
        ('queueDrainedByExport', lambda v: v is False), ('atomicWithStreamFlushOrFileCopy', lambda v: v is False),
        ('completeDeliveryProven', lambda v: v is False),
        ('scope', lambda v: isinstance(v, str) and v == 'current-Native-queue-under-original-mutex;not-in-flight-or-file-delivery'))
    fields, states = result['reported'], result['fieldStates']
    for name, valid in specs:
        if name not in value:states[name] = 'missing';continue
        part = value[name];states[name] = 'recorded' if valid(part) else 'null' if part is None else 'invalid-type-or-value'
        if states[name] == 'recorded':fields[name] = part
    result['unknownRootFields'] = len(value) - sum(name in value for name, _ in specs)
    result['fieldIssues'] = [name + ':' + state for name, state in states.items() if state != 'recorded']
    if 'writerPid' in fields:
        result['writerPid'] = str(fields['writerPid'])
        result['currentPidMatch'] = None if current_pid is None else str(fields['writerPid']) == str(current_pid)
    if fields.get('schema') != 'ymcc.input-capture-queue-export/1':result['wrapperState'] = 'unsupported-schema';return result
    if result['fieldIssues']:result['wrapperState'] = 'invalid-fields';return result
    result['wrapperState'] = 'recognized'
    if fields['queuedRows'] > fields['capacityRows']:result['consistencyProblems'].append('queue-exceeds-reported-capacity')
    if fields['queuedRows'] > 0 and not fields['pendingSignal']:result['consistencyProblems'].append('queued-rows-without-original-pending-signal')
    if not result['consistencyProblems']:
        result['queueObservationStatus'] = 'queued-rows-observed' if fields['queuedRows'] else 'no-queued-rows-observed'
    return result


INPUT_CAPTURE_FAILURE_STAGES = ('stream-open-or-tail', 'stream-not-ready', 'stream-write',
    'stream-flush', 'export-flush', 'export-flush-exception')


def input_capture_log_failure_evidence(availability, selection, current_pid):
    """One selected export snapshot; never a healthy-stream or delivery certificate."""
    limit = 9007199254740991
    scope = 'current-Native-input-capture-IO-failure-observations;not-lost-row-or-complete-delivery-proof'
    clock = 'tickMs=original-failure-observation;Windows-boot-monotonic;zero-is-valid'
    result = {'schemaVersion': 1, 'receipt': 'log-export-availability.json',
        'receiptSelectionStatus': selection['status'], 'archiveEntryOccurrence': selection['selectedOccurrence'],
        'scope': 'offline-current-Native-input-capture-failure-snapshot;not-loss-or-complete-delivery-proof',
        'wrapperState': 'receipt-unavailable', 'reported': {}, 'fieldStates': {},
        'fieldIssues': [], 'consistencyProblems': [], 'consistencyStatus': 'unknown',
        'failureObservationStatus': 'unknown', 'stagesWithRecordedFailures': [],
        'writerPid': None, 'currentPidMatch': None, 'writerProcessCreation100ns': None,
        'writerLifetimeKnown': False, 'manifestSuppliesWriterBirth': False,
        'goodStreamWithFailureHistoryObserved': None, 'completeHistoryProven': False,
        'zeroFailuresProvesHealthyOrComplete': False, 'lostRowsInferred': False,
        'osErrorInferred': False, 'snapshotAtomicWithExportFlush': False,
        'exportFlush': {'state': 'receipt-unavailable', 'reported': {}, 'fieldStates': {}},
        'readingRules': [
            'zero failure observations means none recorded by these IO branches;not complete capture or healthy device proof',
            'streamGood or a closed stream does not clear first/last or earlier failures;not recovery or delivery proof',
            'six counters count observations,not unique faults,lost rows or physical button presses',
            'a saturated counter is a lower bound;first/last retain original boot ticks and zero is valid',
            'writer PID alone does not establish process lifetime;no manifest birth or device cause backfill',
            'explicit export flush and failure snapshot are separate observations;no atomic or queue-drain proof']}
    if selection['status'] != 'selected':
        return result
    availability = availability if isinstance(availability, dict) else {}
    def shape(parent, key):
        return ('missing' if key not in parent else 'null' if parent[key] is None else
                'object' if isinstance(parent[key], dict) else 'invalid-type')
    def uint(value, maximum):
        return type(value) is int and 0 <= value <= maximum
    def project(parent, target, states, key, valid):
        if key not in parent:
            states[key] = 'missing';return
        part = parent[key]
        states[key] = ('recorded' if valid(part) else 'null' if part is None else 'invalid-type-or-value')
        if states[key] == 'recorded':target[key] = part
    flush = result['exportFlush'];flush['state'] = shape(availability, 'inputCaptureStreamFlush')
    if flush['state'] == 'object':
        value = availability['inputCaptureStreamFlush']
        for key, valid in (
            ('schemaVersion', lambda v: type(v) is int and v == 1),
            ('ok', lambda v: type(v) is bool),
            ('scope', lambda v: v == 'current-open-stream-only;not-sample-queue-drain-snapshot-or-durable-proof' if isinstance(v, str) else False)):
            project(value, flush['reported'], flush['fieldStates'], key, valid)
        flush['state'] = 'recognized' if all(v == 'recorded' for v in flush['fieldStates'].values()) else 'invalid-fields'
    result['wrapperState'] = shape(availability, 'inputCaptureLogFailureEvidence')
    if result['wrapperState'] != 'object':
        return result
    value = availability['inputCaptureLogFailureEvidence'];reported = result['reported'];states = result['fieldStates']
    specifications = (
        ('schema', lambda v: isinstance(v, str) and 0 < len(v) <= 96),
        ('writerPid', lambda v: uint(v, (1 << 32) - 1) and v > 0),
        ('writerLifetimeKnown', lambda v: v is False),
        ('failureObservations', lambda v: uint(v, limit)),
        ('counterLimit', lambda v: type(v) is int and v == limit),
        ('counterSaturated', lambda v: type(v) is bool),
        ('streamOpen', lambda v: type(v) is bool), ('streamGood', lambda v: type(v) is bool),
        ('osErrorCodeKnown', lambda v: v is False), ('osErrorCode', lambda v: v is None),
        ('serializationExceptionsCounted', lambda v: v is False), ('completeHistoryProven', lambda v: v is False),
        ('scope', lambda v: v == scope if isinstance(v, str) else False),
        ('clockScope', lambda v: v == clock if isinstance(v, str) else False))
    for key, valid in specifications:project(value, reported, states, key, valid)
    result['unknownRootFields'] = len(value) - sum(key in value for key, _ in specifications) - sum(key in value for key in ('countsByStage', 'first', 'last'))
    if 'writerPid' in reported:
        result['writerPid'] = str(reported['writerPid'])
        result['currentPidMatch'] = None if current_pid is None else str(reported['writerPid']) == str(current_pid)
    result['countsByStage'] = {'state': shape(value, 'countsByStage'), 'reported': {}, 'fieldStates': {}}
    counts = result['countsByStage']
    if counts['state'] == 'object':
        for key in INPUT_CAPTURE_FAILURE_STAGES:
            project(value['countsByStage'], counts['reported'], counts['fieldStates'], key, lambda v: uint(v, limit))
        counts['unknownStageFields'] = len(value['countsByStage']) - sum(key in value['countsByStage'] for key in INPUT_CAPTURE_FAILURE_STAGES)
        counts['state'] = 'recognized' if all(v == 'recorded' for v in counts['fieldStates'].values()) else 'invalid-fields'
    for key in ('first', 'last'):
        witness = {'state': shape(value, key), 'reported': {}, 'fieldStates': {}}
        result[key] = witness
        if witness['state'] == 'object':
            project(value[key], witness['reported'], witness['fieldStates'], 'stage',
                lambda v: isinstance(v, str) and v in INPUT_CAPTURE_FAILURE_STAGES)
            project(value[key], witness['reported'], witness['fieldStates'], 'tickMs', lambda v: uint(v, (1 << 64) - 1))
            witness['state'] = 'recognized' if all(v == 'recorded' for v in witness['fieldStates'].values()) else 'invalid-fields'
    result['fieldIssues'] = [key + ':' + state for key, state in states.items() if state != 'recorded']
    if reported.get('schema') != 'ymcc.input-capture-log-failures/1':
        result['wrapperState'] = 'unsupported-schema';return result
    if result['fieldIssues']:
        result['wrapperState'] = 'invalid-fields';return result
    result['wrapperState'] = 'recognized'
    problems = result['consistencyProblems'];count = reported['failureObservations']
    if counts['state'] != 'recognized':
        problems.append('six-stage-counters-unavailable-or-invalid')
    else:
        stages = counts['reported']
        if counts['unknownStageFields']:problems.append('unknown-stage-counters')
        if any(part > count for part in stages.values()):problems.append('stage-count-exceeds-total')
        summed = sum(stages.values())
        if (summed != count if count < limit else summed < limit):problems.append('stage-counts-total-disagrees')
    if reported['counterSaturated'] != (count == limit):problems.append('counter-saturation-disagrees')
    if count == 0:
        if any(result[key]['state'] != 'null' for key in ('first', 'last')):problems.append('zero-count-has-nonnull-or-missing-witness')
    else:
        if any(result[key]['state'] != 'recognized' for key in ('first', 'last')):
            problems.append('positive-count-witness-unavailable-or-invalid')
        else:
            first, last = result['first']['reported'], result['last']['reported']
            if first['tickMs'] > last['tickMs']:problems.append('first-tick-after-last')
            if count == 1 and first != last:problems.append('single-observation-witnesses-disagree')
            if counts['state'] == 'recognized' and any(counts['reported'][item['stage']] == 0 for item in (first, last)):
                problems.append('witness-stage-has-zero-count')
    result['consistencyStatus'] = 'inconsistent' if problems else 'consistent'
    if not problems:
        result['failureObservationStatus'] = 'failures-recorded' if count else 'no-recorded-failures'
        result['stagesWithRecordedFailures'] = [key for key in INPUT_CAPTURE_FAILURE_STAGES if counts['reported'][key] > 0]
        result['goodStreamWithFailureHistoryObserved'] = count > 0 and reported['streamGood']
    return result


NATIVE_APPEND_FAILURE_BUCKETS = (('unassigned', 'record-wrapper-exception'), ('event', 'open'), ('detail', 'open'),
    ('event', 'write'), ('detail', 'write'), ('event', 'flush'), ('detail', 'flush'),
    ('event', 'unusable-open-stream'), ('detail', 'unusable-open-stream'), ('event', 'sink-exception'), ('detail', 'sink-exception'))


def native_flush_close_evidence(stats):
    """Three independent observations from the original explicit flush wrapper."""
    value = stats.get('flushCloseBoundary')
    result = {'state':'missing' if 'flushCloseBoundary' not in stats else 'null' if value is None else 'invalid-type',
        'reported':{}, 'fieldIssues':[], 'saturatedCountFields':[], 'countsSummed':False,
        'priorBadStateProvesCloseFailure':False, 'wrapperExceptionIdentifiesFile':False,
        'zeroProvesCloseOrDurability':False, 'resourceReturnOrHistoryCompleteProven':False}
    if not isinstance(value, dict):return result
    if type(value.get('schemaVersion')) is not int or value['schemaVersion'] != 1:
        result['state']='unsupported-schema';return result
    scope='original-flush-wrapper;close-result-unknown-after-prior-bad-state;outside-Native-file-exceptions-unassigned'
    if value.get('scope') != scope:
        result['state']='unsupported-scope';return result
    for key in ('knownCloseFailures','closePriorBadStateObservations','unassignedWrapperExceptions'):
        part=value.get(key)
        if type(part) is int and 0 <= part < (1 << 64):
            result['reported'][key]=part
            if part == (1 << 64)-1:result['saturatedCountFields'].append(key)
        else:result['fieldIssues'].append(key+':invalid-or-missing')
    result['state']='reported-with-issues' if result['fieldIssues'] else 'reported'
    result['readingRules']=['close with earlier bad stream state stays unknown even after clear resets the flags',
        'knownCloseFailures records a failed original close result from a previously good open stream;not all close sites',
        'unassignedWrapperExceptions can come from adaptation/domain boundaries;no Native file fault is inferred',
        'flush attempts reuse the original event/detail flush buckets;counts are observations and are never added as unique lost rows']
    return result


def native_event_append_recovery_evidence(stats):
    """Versioned file/preparation rearm observations,never business recovery."""
    value=stats.get('eventAppendRecovery')
    result={'state':'missing' if 'eventAppendRecovery' not in stats else 'null' if value is None else 'invalid-type',
        'reported':{},'fieldIssues':[], 'reportedSchemaVersion':None, 'preparationFailureState':'not-reported',
        'currentFileHealthProven':False, 'oldLostRowRestored':False, 'screenReofferActuallyWrittenInferred':False,
        'businessOrDeviceRecoveryInferred':False, 'epochIsOperationCount':False,
        'preparationFailureProvesFileFailure':False,'pendingFailureProvesCurrentMemoryExhaustion':False,
        'reportedScope':None,'sensorRetirementPreparationFailureState':'not-reported',
        'sensorRetirementPreparationFailureProvesFileFailure':False,'sensorRetirementReofferActuallyWrittenInferred':False,
        'msiFeedbackPreparationFailureState':'not-reported','msiFeedbackPreparationFailureProvesFileFailure':False,
        'msiFeedbackReofferActuallyWrittenInferred':False}
    if not isinstance(value,dict):return result
    version=value.get('schemaVersion')
    if type(version) is not int or version not in (1,2,3,4,5,6,7):result['state']='unsupported-schema';return result
    result['reportedSchemaVersion']=version
    sensorScope='screen-and-sensor-retirement-memo-rearm-after-original-good-event-append;file-exit-or-named-preparation-fault'
    msiScope='screen-sensor-retirement-and-msi-feedback-memo-rearm-after-original-good-event-append;file-exit-or-named-preparation-fault'
    snapshotScope='native-classifier-snapshot-screen-sensor-retirement-and-msi-feedback-memo-rearm-after-original-good-event-append;file-exit-or-named-preparation-fault'
    diagnosticMemoScope='native-classifier-snapshot-and-diagnostic-memo-screen-sensor-retirement-and-msi-feedback-rearm-after-original-good-event-append;file-exit-or-named-preparation-fault'
    recordPreparationScope='native-selected-record-and-serialization-preparation-with-classifier-snapshot-diagnostic-screen-sensor-msi-rearm-after-original-good-event-append;not-file-or-delivery-proof'
    pathPreparationScope='native-selected-record-serialization-and-path-preparation-with-classifier-snapshot-diagnostic-screen-sensor-msi-rearm-after-original-good-event-append;not-file-or-delivery-proof'
    scopes={1:('screen-memo-rearm-only-after-original-good-event-append-and-flush',),
        2:('screen-memo-rearm-after-original-good-event-append;file-exit-or-screen-preparation-fault',sensorScope),3:(msiScope,),4:(snapshotScope,),5:(diagnosticMemoScope,),6:(recordPreparationScope,),7:(pathPreparationScope,)}
    scope=value.get('scope')
    if scope not in scopes[version]:result['state']='unsupported-scope';return result
    result['reportedScope']=scope
    sensorDescribed=scope in (sensorScope,msiScope,snapshotScope,diagnosticMemoScope,recordPreparationScope,pathPreparationScope)
    msiDescribed=scope in (msiScope,snapshotScope,diagnosticMemoScope,recordPreparationScope,pathPreparationScope)
    result['nativeClassifierSnapshotRearmState']='described-by-scope' if scope in (snapshotScope,diagnosticMemoScope,recordPreparationScope,pathPreparationScope) else 'not-described-by-scope'
    result['nativeClassifierSnapshotReofferWrittenInferred']=False
    result['nativeClassifierDiagnosticMemoRearmState']='described-by-scope' if scope in (diagnosticMemoScope,recordPreparationScope,pathPreparationScope) else 'not-described-by-scope'
    result['nativeClassifierDiagnosticMemoReofferWrittenInferred']=False
    epoch=value.get('epoch')
    if type(epoch) is int and 0 <= epoch < (1<<64):result['reported']['epoch']=epoch
    else:result['fieldIssues'].append('epoch:invalid-or-missing')
    booleanFields=('unrecoveredFailureObserved','epochLimitReached')
    if version>=2:booleanFields+=('unrecoveredPreparationFailureObserved',)
    if sensorDescribed:booleanFields+=('unrecoveredSensorRetirementPreparationFailureObserved',)
    if msiDescribed:booleanFields+=('unrecoveredMsiFeedbackPreparationFailureObserved',)
    if scope in (recordPreparationScope,pathPreparationScope):booleanFields+=('unrecoveredResidentRecordPreparationFailureObserved',)
    if scope==pathPreparationScope:booleanFields+=('unrecoveredResidentPathPreparationFailureObserved',)
    for key in booleanFields:
        if type(value.get(key)) is bool:result['reported'][key]=value[key]
        else:result['fieldIssues'].append(key+':invalid-or-missing')
    result['preparationFailureState']=('not-reported-by-schema1' if version==1 else
        'reported' if 'unrecoveredPreparationFailureObserved' in result['reported'] else 'unknown')
    result['sensorRetirementPreparationFailureState']=('reported' if 'unrecoveredSensorRetirementPreparationFailureObserved' in result['reported'] else 'unknown') if sensorDescribed else 'not-described-by-scope'
    result['msiFeedbackPreparationFailureState']=('reported' if 'unrecoveredMsiFeedbackPreparationFailureObserved' in result['reported'] else 'unknown') if msiDescribed else 'not-described-by-scope'
    for key in ('currentFileHealthProven','oldLostRowRestored'):
        if value.get(key) is not False:result['fieldIssues'].append(key+':missing-or-unsupported')
    if 'epoch' in result['reported'] and result['reported'].get('epochLimitReached') is not (epoch==(1<<64)-1):
        result['fieldIssues'].append('epoch-limit-flag-disagrees')
    sources={'state':'not-described-by-scope','reported':{},'fieldIssues':[],
        'countsAreUniqueRows':False,'fileFailureProven':False,'countsSummedWithWrapperBucket':False}
    result['recordPreparationSources']=sources
    result['residentRecordPreparationFailureState']='not-described-by-scope'
    if scope in (recordPreparationScope,pathPreparationScope):
        result['residentRecordPreparationFailureState']='reported' if 'unrecoveredResidentRecordPreparationFailureObserved' in result['reported'] else 'unknown'
        valueSources=value.get('recordPreparationSources')
        sources['state']='missing' if 'recordPreparationSources' not in value else 'null' if valueSources is None else 'invalid-type'
        if isinstance(valueSources,dict):
            expected={'schemaVersion':1,'counterLimit':(1<<64)-1,'countsAreUniqueRows':False,'fileFailureProven':False}
            for key,required in expected.items():
                part=valueSources.get(key)
                if type(part) is type(required) and part==required:sources['reported'][key]=part
                else:sources['fieldIssues'].append(key+':missing-or-unsupported')
            for key in ('residentRecord','detailRecord','residentSerialization','detailSerialization'):
                part=valueSources.get(key)
                if type(part) is int and 0<=part<(1<<64):sources['reported'][key]=part
                else:sources['fieldIssues'].append(key+':invalid-or-missing')
            sources['state']='reported-with-issues' if sources['fieldIssues'] else 'reported'
        if sources['state']!='reported':result['fieldIssues'].append('recordPreparationSources:'+sources['state'])
    pathSources={'state':'not-described-by-scope','reported':{},'fieldIssues':[],
        'countsAreUniqueRows':False,'fileFailureProven':False,'countsSummedWithWrapperBucket':False}
    result['pathPreparationSources']=pathSources
    result['residentPathPreparationFailureState']='not-described-by-scope'
    result['pathPreparationFailureProvesFileFailure']=False
    if scope==pathPreparationScope:
        result['residentPathPreparationFailureState']='reported' if 'unrecoveredResidentPathPreparationFailureObserved' in result['reported'] else 'unknown'
        pathValue=value.get('pathPreparationSources')
        pathSources['state']='missing' if 'pathPreparationSources' not in value else 'null' if pathValue is None else 'invalid-type'
        if isinstance(pathValue,dict):
            expected={'schemaVersion':1,'counterLimit':(1<<64)-1,'countsAreUniqueRows':False,'fileFailureProven':False}
            for key,required in expected.items():
                part=pathValue.get(key)
                if type(part) is type(required) and part==required:pathSources['reported'][key]=part
                else:pathSources['fieldIssues'].append(key+':missing-or-unsupported')
            for key in ('residentPath','detailPath'):
                part=pathValue.get(key)
                if type(part) is int and 0<=part<(1<<64):pathSources['reported'][key]=part
                else:pathSources['fieldIssues'].append(key+':invalid-or-missing')
            pathSources['state']='reported-with-issues' if pathSources['fieldIssues'] else 'reported'
        if pathSources['state']!='reported':result['fieldIssues'].append('pathPreparationSources:'+pathSources['state'])
    result['state']='reported-with-issues' if result['fieldIssues'] else 'reported'
    result['readingRules']=['schema1 does not report screen preparation state;missing never supplies false',
        'schema2 accepted scopes distinguish file-exit and screen preparation;the exact screen-and-sensor-retirement scope also describes sensor preparation separately',
        'a legacy scope never supplies a sensor preparation false;missing or mistyped flags in the sensor scope remain unknown',
        'schema3 describes MSI diagnostic preparation separately;its rearm epoch never grants output retry or proves the motor/file recovered',
        'schema4 also describes the bounded general Native snapshot classifier;it does not cover every memo or new preparation fault and does not prove the next reoffer was written',
        'schema5 adds only the five classifier diagnostic memo slots;IPC lifetime type capacity,other source gates and business retries are unchanged',
        'schema6 separates selected resident/detail record and serialization catches;source counts are subsets of the wrapper bucket and never added as lost rows or file failures',
        'schema7 reports resident/detail path construction exceptions separately before open/rotation/write;path counts are not file failures or unique lost rows',
        'an original successful resident append/flush clears pending causes and advances one memo rearm epoch',
        'detail-OFF get logging can pause until that natural boundary;returned state and setting actions are separate',
        'pending flags are historical observer state,not current memory/file health or a request to retry',
        'a saturated epoch cannot distinguish later edges;no original missing row,time,action or complete delivery is reconstructed']
    return result


def native_preselection_stage_evidence(stats):
    """Two optional caught-call stages;never lane,allocation cause or delivery."""
    value=stats.get('preselectionExceptionStages')
    result={'state':'missing' if 'preselectionExceptionStages' not in stats else 'null' if value is None else 'invalid-type',
        'reported':{},'fieldIssues':[],'stages':[],'sourceRowsObserved':None,'sourceRowsNotInspected':None,
        'maxInspectedStages':2,'countsAreUniqueRows':False,'countsSummedWithPreparationBucket':False,
        'fileLaneInferred':False,'fileFailureProven':False,'allocationFailureProven':False,'recoveryPermissionInferred':False}
    if not isinstance(value,dict):return result
    if type(value.get('schemaVersion')) is not int or value['schemaVersion']!=1:result['state']='unsupported-schema';return result
    expected={'schemaVersion':1,'scope':'current-Native-preselection-projection-and-classifier-catches;not-lane-file-or-delivery-proof',
        'maxStages':2,'maxEventPrefixBytes':95,'counterLimit':(1<<64)-1,'countsAreUniqueRows':False,
        'countsSummedWithPreparationBucket':False,'fileFailureProven':False}
    for key,required in expected.items():
        part=value.get(key)
        if type(part) is type(required) and part==required:result['reported'][key]=part
        else:result['fieldIssues'].append(key+':missing-or-unsupported')
    rows=value.get('stages')
    if not isinstance(rows,list):result['state']='stages-unrecognized';return result
    result['sourceRowsObserved']=len(rows);result['sourceRowsNotInspected']=max(0,len(rows)-2)
    if len(rows)!=2:result['fieldIssues'].append('stage-count-not-two')
    known=('steam-log-projection','native-log-classification');seen={}
    for index,row in enumerate(rows[:2]):
        item={'sourceIndex':index,'state':'invalid-type','reported':{},'fieldIssues':[],'consistent':False};result['stages'].append(item)
        if not isinstance(row,dict):continue
        stage=row.get('stage')
        if type(stage) is not str or stage not in known:item['state']='unknown-stage';continue
        item['stage']=stage;item['state']='reported'
        if stage in seen:item['state']=seen[stage]['state']='ambiguous-duplicate-stage';seen[stage]['consistent']=False
        else:seen[stage]=item
        found=item['reported'];bad=item['fieldIssues'];count=row.get('observedFailures')
        if type(count) is int and 0<=count<(1<<64):found['observedFailures']=count
        else:bad.append('observedFailures:invalid-or-missing')
        for key in ('counterLimitReached','firstEventKnown','firstEventTruncated','firstEventAsciiSanitized'):
            if type(row.get(key)) is bool:found[key]=row[key]
            else:bad.append(key+':invalid-or-missing')
        event=row.get('firstEvent')
        if found.get('firstEventKnown') is True:
            if type(event) is str and len(event)<=95 and all(32<=ord(char)<=126 for char in event):found['firstEvent']=event
            else:bad.append('firstEvent:invalid-or-missing')
        elif found.get('firstEventKnown') is False:
            if 'firstEvent' in row and event is None:found['firstEvent']=None
            else:bad.append('unknown-first-event-not-explicit-null')
        if 'observedFailures' in found:
            if found.get('counterLimitReached') is not (count==(1<<64)-1):bad.append('counter-saturation-disagrees')
            if count==0 and found.get('firstEventKnown') is not False:bad.append('zero-count-has-first-event')
        if found.get('firstEventKnown') is False and (found.get('firstEventTruncated') is not False or found.get('firstEventAsciiSanitized') is not False):bad.append('unknown-first-event-has-text-flags')
        if bad and item['state']=='reported':item['state']='reported-with-issues'
        item['consistent']=item['state']=='reported'
    result['state']='reported' if not result['fieldIssues'] and len(seen)==2 and all(item['consistent'] for item in result['stages']) else 'reported-with-issues'
    result['readingRules']=['stage identifies where the original catch observed an exception;it never proves allocation cause,file lane,current health or a lost operation',
        'counts overlap the original preparation bucket;never add them as distinct failed rows or recover missing original payloads',
        'missing legacy stage receipt supplies neither false nor zero;only two fixed stage names and bounded first-event prefixes are inspected']
    return result


def native_append_failure_evidence(availability, selection, current_pid):
    """Original append failure observations;neither delivery nor unique row loss."""
    view = {'schemaVersion': 1, 'receiptSelectionStatus': selection.get('status'),
        'archiveEntryOccurrence': selection.get('selectedOccurrence'), 'state': 'receipt-unavailable',
        'writerPid': None, 'writerProcessCreation100ns': None, 'writerLifetimeKnown': False, 'currentPidMatch': None,
        'reported': {}, 'fieldIssues': [], 'buckets': [], 'sourceRowsObserved': None, 'sourceRowsNotInspected': None,
        'flushCloseBoundary': {'state':'receipt-unavailable','reported':{},'zeroProvesCloseOrDurability':False},
        'eventAppendRecovery': {'state':'receipt-unavailable','reported':{},'currentFileHealthProven':False},
        'preselectionExceptionStages': {'state':'receipt-unavailable','stages':[],'fileLaneInferred':False,'fileFailureProven':False},
        'maxInspectedBuckets': 11, 'countsSummedAcrossBuckets': False, 'uniqueLostRowsInferred': False,
        'zeroProvesSinkHealth': False, 'dedupSelectionProvesDelivery': False, 'osErrorCodeInferred': False,
        'manifestSuppliesWriterBirth': False, 'completeHistoryOrPhysicalOutcomeProven': False}
    if selection.get('status') != 'selected':return view
    availability = availability if isinstance(availability, dict) else {}
    value = availability.get('nativeLogFailureEvidence')
    view['state'] = 'missing' if 'nativeLogFailureEvidence' not in availability else 'null' if value is None else 'invalid-type'
    if not isinstance(value, dict):return view
    issues = view['fieldIssues']
    process, birth = value.get('writerPid'), value.get('writerProcessCreation100ns')
    if type(process) is int and 0 < process < (1 << 32):
        view['writerPid'] = str(process);view['currentPidMatch'] = None if current_pid is None else str(process) == str(current_pid)
    else:issues.append('writerPid:invalid-or-missing')
    if isinstance(birth, str) and 0 < len(birth) <= 20 and birth.isascii() and birth.isdecimal() and 0 < int(birth) < (1 << 64):
        view['writerProcessCreation100ns'] = str(int(birth))
    elif birth is not None:issues.append('writerProcessCreation100ns:invalid')
    view['writerLifetimeKnown'] = view['writerPid'] is not None and view['writerProcessCreation100ns'] is not None
    stats = value.get('stats')
    if not isinstance(stats, dict):view['state'] = 'stats-unrecognized';return view
    if type(stats.get('schemaVersion')) is not int or stats['schemaVersion'] != 1:
        view['state'] = 'unsupported-stats-schema';return view
    view['flushCloseBoundary']=native_flush_close_evidence(stats)
    view['eventAppendRecovery']=native_event_append_recovery_evidence(stats)
    view['preselectionExceptionStages']=native_preselection_stage_evidence(stats)
    expected = {'counterLimit':(1 << 64) - 1, 'maxBuckets':11, 'maxEventPrefixBytes':95,
        'scope':'current-Native-original-file-exit-failure-observations',
        'snapshotAtomicWithLogFiles':False, 'successfulAttemptsCounted':False, 'osErrorCodeAvailable':False,
        'failureCountsAreUniqueRows':False, 'zeroProvesCompleteHistory':False, 'deduplicationIsDeliveryProof':False}
    for key, required in expected.items():
        part = stats.get(key)
        if key not in stats or type(part) is not type(required) or part != required:
            issues.append(key + ':missing-or-unsupported')
        else:view['reported'][key] = part
    rows = stats.get('buckets')
    if not isinstance(rows, list):view['state'] = 'buckets-unrecognized';return view
    view['sourceRowsObserved'] = len(rows);view['sourceRowsNotInspected'] = max(0, len(rows) - 11)
    if len(rows) != 11:issues.append('bucket-count-not-eleven')
    seen = {}
    for index, row in enumerate(rows[:11]):
        item = {'sourceIndex':index, 'state':'invalid-type', 'reported':{}, 'fieldIssues':[], 'consistent':False}
        view['buckets'].append(item)
        if not isinstance(row, dict):continue
        pair = (row.get('lane'), row.get('stage'))
        if pair not in NATIVE_APPEND_FAILURE_BUCKETS:item['state'] = 'unknown-lane-or-stage';continue
        item['lane'], item['stage'] = pair
        # The known pair consists of strings, so it is now safe to index by it.
        if pair in seen:
            item['state'] = seen[pair]['state'] = 'ambiguous-duplicate-bucket'
            seen[pair]['consistent'] = False
        else:seen[pair] = item;item['state'] = 'reported'
        found = item['reported'];bad = item['fieldIssues'];count = row.get('observedFailures')
        if type(count) is int and 0 <= count < (1 << 64):found['observedFailures'] = count
        else:bad.append('observedFailures:invalid-or-missing')
        for key in ('counterLimitReached', 'firstEventKnown', 'firstEventTruncated', 'firstEventAsciiSanitized'):
            if type(row.get(key)) is bool:found[key] = row[key]
            else:bad.append(key + ':invalid-or-missing')
        event = row.get('firstEvent')
        if found.get('firstEventKnown') is True:
            if isinstance(event, str) and len(event) <= 95 and all(32 <= ord(char) <= 126 for char in event):found['firstEvent'] = event
            else:bad.append('firstEvent:invalid-or-missing')
        elif found.get('firstEventKnown') is False:
            if 'firstEvent' in row and event is None:found['firstEvent'] = None
            else:bad.append('unknown-first-event-not-explicit-null')
        if 'observedFailures' in found:
            if found.get('counterLimitReached') is not (count == (1 << 64) - 1):bad.append('counter-saturation-disagrees')
            if count == 0 and found.get('firstEventKnown') is not False:bad.append('zero-count-has-first-event')
        if found.get('firstEventKnown') is False and (found.get('firstEventTruncated') is not False or found.get('firstEventAsciiSanitized') is not False):
            bad.append('unknown-first-event-has-text-flags')
        if bad and item['state'] == 'reported':item['state'] = 'reported-with-issues'
        item['consistent'] = item['state'] == 'reported'
    view['state'] = 'reported' if not issues and len(seen) == 11 and all(item['consistent'] for item in view['buckets']) else 'reported-with-issues'
    if view['flushCloseBoundary']['state'] not in ('reported','missing'):view['state']='reported-with-issues'
    if view['eventAppendRecovery']['state'] not in ('reported','missing'):view['state']='reported-with-issues'
    if view['preselectionExceptionStages']['state'] not in ('reported','missing'):view['state']='reported-with-issues'
    view['readingRules'] = ['one failure can precede later memo suppression;no retry or original missing payload is reconstructed',
        'counts are local observations,not unique failed rows,call frequency or OS errors;do not sum with other counters',
        'wrapper exception has no assigned file lane;optional preselection stages identify only the entered projection/classifier call,not allocation cause or current health',
        'postwrite mirror failures are excluded from sink-exception buckets',
        'firstEvent is a bounded producer prefix;truncated or ASCII-sanitized text is not the original full event',
        'original explicit flush/close boundary is reported separately;direct other close sites,domain/deferred sinks and other processes are not fully covered']
    return view


DECKY_LOG_FAILURE_STAGES = ('preparation-exception', 'open-or-boundary-failure', 'open-or-boundary-exception',
                            'write-failure', 'write-exception', 'flush-failure', 'flush-exception')


SHORTCUT_READ_TAGS=('media-shortcut-truncated','media-shortcut-string','media-shortcut-depth','media-shortcut-size','media-shortcut-duplicate','media-shortcut-type','media-shortcut-unterminated','media-shortcut-tail','media-shortcut-id','media-shortcut-gameid','media-shortcut-executable-unconfirmed','media-shortcuts-unavailable','media-shortcuts-changed','media-shortcuts-invalid','shortcut-read-exception')


def shortcut_read_record(row):
    """Only same-row safe original facts; no fresh path or action inference."""
    result=dict(state='missing',reported={},fieldIssues=[],currentPathProven=False,writerLifetimeVerified=False)
    if not isinstance(row,dict) or 'shortcutRead' not in row:return result
    raw=row['shortcutRead']
    if raw is None:result['state']='null';return result
    if not isinstance(raw,dict):result['state']='invalid-type';return result
    if type(raw.get('schema')) is not int or raw['schema']!=1:result['state']='unsupported-schema';return result
    if raw.get('scope')!='original-shortcut-inventory-read-facts;not-path-or-owner-proof':result['state']='unsupported-scope';return result
    bools=('sourceReadOnly','readAttempted','filePresent','regularFile','afterFailureReadProbe')
    signed=('fileStatusErrorCode','fileSizeErrorCode');unsigned=('fileBytes','afterFailureReadErrorCode')
    fields=bools+signed+unsigned+('readErrorTag',);metadata=('schema','scope','reportedFields','pathsAndAccountsOmitted','filesystemErrorCategoryKnown')
    issues=result['fieldIssues']
    if len(raw)>15 or any(k not in fields+metadata for k in raw):issues.append('unknown-or-excess-fields')
    for key in bools:
        if key in raw:
            if type(raw[key]) is bool:result['reported'][key]=raw[key]
            else:issues.append(key+':invalid-bool')
    for key in signed+unsigned:
        if key not in raw:continue
        value=raw[key];lower=-(1<<31) if key in signed else 0;upper=(1<<31)-1 if key in signed else (1<<64)-1 if key=='fileBytes' else (1<<32)-1
        if type(value) is int and lower<=value<=upper:result['reported'][key]=value
        else:issues.append(key+':invalid-integer')
    if 'readErrorTag' in raw:
        if isinstance(raw['readErrorTag'],str) and raw['readErrorTag'] in SHORTCUT_READ_TAGS:result['reported']['readErrorTag']=raw['readErrorTag']
        else:issues.append('readErrorTag:unknown')
    for key in ('sourceReadOnly','readAttempted'):
        if key not in raw:issues.append(key+':missing')
    count=raw.get('reportedFields')
    if type(count) is not int or not 1<=count<=10 or count!=sum(k in raw for k in fields):issues.append('reportedFields:invalid-or-disagrees')
    if raw.get('pathsAndAccountsOmitted') is not True:issues.append('pathsAndAccountsOmitted:not-true')
    if raw.get('filesystemErrorCategoryKnown') is not False:issues.append('filesystemErrorCategoryKnown:not-false')
    result['state']='invalid-fields' if issues else 'reported-original-shortcut-read-facts'
    return result


def decky_shortcut_read_evidence(members,current_pid=None):
    states=('missing','null','invalid-type','unsupported-schema','unsupported-scope','invalid-fields','reported-original-shortcut-read-facts')
    counts=dict.fromkeys(states,0);head=[];tail=collections.deque(maxlen=8);total=0
    for filename,rows,metadata in members:
        if not isinstance(filename,str) or re.fullmatch(r'decky-sidebar\.log(?:\.(?:[0-9]+|prev|previous))?',filename) is None:continue
        for line,row in rows:
            if row.get('event')!='wallpaper-inventory':continue
            view=shortcut_read_record(row);total+=1;counts[view['state']]+=1;writer,field=process_identity(row,filename)
            witness=dict(file=filename[:160],line=line if type(line) is int and 0<line<(1<<64) else None,writerPid=None if writer=='unknown' else writer,writerField=field,currentPidMatch=None if writer=='unknown' or current_pid is None else writer==str(current_pid),record=view)
            occurrence=metadata.get('archiveEntryOccurrence')
            if type(occurrence) is int and 0<=occurrence<(1<<64):witness['archiveEntryOccurrence']=occurrence
            if len(head)<8:head.append(witness)
            else:tail.append(witness)
    return dict(scope='retained-wallpaper-inventory-original-read-facts;no-join-or-error-inference',rowsObserved=total,countsByState=counts,witnesses=head+list(tail),rowsNotListed=max(0,total-len(head)-len(tail)),maxWitnesses=16,missingMeans='unknown;not-no-read-or-success',samePidMeansSameLifetime=False,probeErrorIsOriginalReadError=False,rawRowsRemoved=False)

def decky_log_failure_evidence(availability, selection, current_pid):
    """Bounded sidecar observations; missing or invalid evidence stays unknown."""
    view = {'schemaVersion': 1, 'scope': 'bounded-Decky-sidecar-failure-observations',
            'receiptSelectionStatus': selection.get('status'), 'state': 'receipt-unavailable',
            'writerPid': None, 'writerProcessCreation100ns': None, 'reportedWriterIdentityKnown': False,
            'writerLifetimeVerified': False, 'currentPidMatch': None, 'reported': {}, 'fieldIssues': [], 'buckets': [],
            'maxInspectedBuckets': 7, 'sourceRowsObserved': None, 'sourceRowsNotInspected': None,
            'countsSummedAcrossBuckets': False, 'uniqueLostRowsInferred': False, 'zeroProvesSinkHealth': False,
            'osErrorCodeInferred': False, 'completeHistoryOrPhysicalOutcomeProven': False}
    if selection.get('status') != 'selected':return view
    availability = availability if isinstance(availability, dict) else {}
    value = availability.get('deckyLogFailureEvidence')
    view['state'] = 'missing' if 'deckyLogFailureEvidence' not in availability else 'null' if value is None else 'invalid-type'
    if not isinstance(value, dict):return view
    issues = view['fieldIssues']
    process, birth = value.get('writerPid'), value.get('writerProcessCreation100ns')
    if type(process) is int and 0 < process < (1 << 32):
        view['writerPid'] = str(process);view['currentPidMatch'] = None if current_pid is None else str(process) == str(current_pid)
    else:issues.append('writerPid:invalid-or-missing')
    if isinstance(birth, str) and 0 < len(birth) <= 20 and birth.isascii() and birth.isdecimal() and 0 < int(birth) < (1 << 64):
        view['writerProcessCreation100ns'] = str(int(birth))
    elif birth is not None:issues.append('writerProcessCreation100ns:invalid')
    view['reportedWriterIdentityKnown'] = view['writerPid'] is not None and view['writerProcessCreation100ns'] is not None
    stats = value.get('stats')
    if not isinstance(stats, dict):view['state'] = 'stats-unrecognized';return view
    if type(stats.get('schemaVersion')) is not int or stats['schemaVersion'] != 1:
        view['state'] = 'unsupported-stats-schema';return view
    expected = {'scope': 'current-Native-Decky-sidecar-failure-observations', 'counterLimit': (1 << 64) - 1,
                'maxBuckets': 7, 'maxEventPrefixBytes': 95, 'snapshotAtomicWithLogFiles': False,
                'successfulAttemptsCounted': False, 'osErrorCodeAvailable': False, 'failureCountsAreUniqueRows': False,
                'zeroProvesCompleteHistory': False, 'currentFileHealthProven': False}
    for key, required in expected.items():
        part = stats.get(key)
        if key not in stats or type(part) is not type(required) or part != required:issues.append(key + ':missing-or-unsupported')
        else:view['reported'][key] = part
    rows = stats.get('buckets')
    if not isinstance(rows, list):view['state'] = 'buckets-unrecognized';return view
    view['sourceRowsObserved'] = len(rows);view['sourceRowsNotInspected'] = max(0, len(rows) - 7)
    if len(rows) != 7:issues.append('bucket-count-not-seven')
    seen = {}
    for index, row in enumerate(rows[:7]):
        item = {'sourceIndex': index, 'state': 'invalid-type', 'reported': {}, 'fieldIssues': [], 'consistent': False}
        view['buckets'].append(item)
        if not isinstance(row, dict):continue
        stage = row.get('stage')
        if not isinstance(stage, str) or stage not in DECKY_LOG_FAILURE_STAGES:item['state'] = 'unknown-stage';continue
        item['stage'] = stage
        if stage in seen:
            item['state'] = seen[stage]['state'] = 'ambiguous-duplicate-bucket';seen[stage]['consistent'] = False
        else:seen[stage] = item;item['state'] = 'reported'
        found, bad, count = item['reported'], item['fieldIssues'], row.get('observedFailures')
        if type(count) is int and 0 <= count < (1 << 64):found['observedFailures'] = count
        else:bad.append('observedFailures:invalid-or-missing')
        for key in ('counterLimitReached', 'firstEventKnown', 'firstEventTruncated', 'firstEventAsciiSanitized'):
            if type(row.get(key)) is bool:found[key] = row[key]
            else:bad.append(key + ':invalid-or-missing')
        event = row.get('firstEvent')
        if found.get('firstEventKnown') is True:
            if isinstance(event, str) and len(event) <= 95 and all(32 <= ord(char) <= 126 for char in event):found['firstEvent'] = event
            else:bad.append('firstEvent:invalid-or-missing')
        elif found.get('firstEventKnown') is False:
            if 'firstEvent' in row and event is None:found['firstEvent'] = None
            else:bad.append('unknown-first-event-not-explicit-null')
        if 'observedFailures' in found:
            if found.get('counterLimitReached') is not (count == (1 << 64) - 1):bad.append('counter-saturation-disagrees')
            if count == 0 and (found.get('firstEventKnown') is not False or found.get('firstEventTruncated') is not False or found.get('firstEventAsciiSanitized') is not False):bad.append('zero-count-has-first-event')
        if found.get('firstEventKnown') is False and (found.get('firstEventTruncated') is not False or found.get('firstEventAsciiSanitized') is not False):bad.append('unknown-first-event-has-text-flags')
        if found.get('firstEventTruncated') is True and len(found.get('firstEvent') or '') != 95:bad.append('truncated-prefix-not-full')
        if bad and item['state'] == 'reported':item['state'] = 'reported-with-issues'
        item['consistent'] = item['state'] == 'reported'
    view['state'] = 'reported' if not issues and len(seen) == 7 and all(item['consistent'] for item in view['buckets']) else 'reported-with-issues'
    view['readingRules'] = ['local failure observations are not unique rows, business failures or complete history; do not sum with Native counters',
        'open-or-boundary cannot separate path open, tail read or separator failure; no OS code is inferred',
        'preparation exceptions do not prove a file error; destructor close and rotation internals are outside this witness',
        'firstEvent is a bounded ASCII prefix, not full text when truncated or sanitized; missing snapshot remains unknown',
        'a reported PID and birth are not independent OS lifetime or loaded source proof']
    return view


COORDINATOR_LOG_FAILURE_STAGES=('preparation-exception','source-open-or-presence-failure','source-read-failure',
    'rewrite-open-failure','rewrite-write-failure','source-read-exception','rewrite-open-or-write-exception')


COORDINATOR_COMMIT_FAILURE_STAGES=('temporary-create-failure','temporary-write-failure','temporary-close-failure',
    'promotion-failure','cleanup-failure','new-row-over-budget','commit-exception')


def coordinator_rewrite_commit_evidence(availability, selection, current_pid):
    """Single staged commit failure marginals;no crash or all-writer durability."""
    mapped={};issues=[];sourceRowCount=None
    if isinstance(availability,dict) and 'coordinatorRewriteCommitEvidence' in availability:
        value=availability['coordinatorRewriteCommitEvidence']
        if isinstance(value,dict):
            stats=value.get('stats')
            if isinstance(stats,dict):
                expected={'temporaryCreateDisposition':'CREATE_NEW','promotionFlags':'MOVEFILE_REPLACE_EXISTING',
                    'explicitFlushPerformed':False,'maxCommitAttemptsPerCall':1,'sourceAndCommitAtomic':False,'maxOutstandingTemporaryPerWriter':1}
                for key,wanted in expected.items():
                    if key not in stats or type(stats[key]) is not type(wanted) or stats[key]!=wanted:issues.append(key+':missing-or-unsupported')
                keys=('schemaVersion','scope','counterLimit','maxBuckets','maxEventPrefixBytes','snapshotAtomicWithLogFiles',
                    'successfulAttemptsCounted','osErrorCodeAvailable','failureCountsAreUniqueRows','zeroProvesCompleteHistory','currentFileHealthProven','buckets')
                stats={key:stats[key] for key in keys if key in stats}
                stats['scope']='current-Native-Decky-sidecar-failure-observations' if stats.get('scope')=='current-Native-Coordinator-staged-commit-failure-observations' else None
                if isinstance(stats.get('buckets'),list):
                    rows=stats['buckets'];sourceRowCount=len(rows);copied=[]
                    for row in rows[:7]:
                        if isinstance(row,dict):
                            keys=('stage','observedFailures','counterLimitReached','firstEventKnown','firstEvent','firstEventTruncated','firstEventAsciiSanitized')
                            row={key:row[key] for key in keys if key in row};stage=row.get('stage')
                            row['stage']=DECKY_LOG_FAILURE_STAGES[COORDINATOR_COMMIT_FAILURE_STAGES.index(stage)] if isinstance(stage,str) and stage in COORDINATOR_COMMIT_FAILURE_STAGES else None
                        copied.append(row)
                    if sourceRowCount!=7:issues.append('bucket-count-not-seven')
                    stats['buckets']=copied
            value={key:value[key] for key in ('writerPid','writerProcessCreation100ns') if key in value}|{'stats':stats}
        mapped['deckyLogFailureEvidence']=value
    view=decky_log_failure_evidence(mapped,selection,current_pid);view['scope']='bounded-Coordinator-staged-commit-failure-observations';view['maxObservedStageKinds']=7
    view['crashDurabilityOrConcurrentWriterExclusionProven']=False;view['cleanupAbsenceOrUniqueLostRowsProven']=False
    if sourceRowCount is not None and selection.get('status')=='selected':view['sourceRowsObserved']=sourceRowCount;view['sourceRowsNotInspected']=max(0,sourceRowCount-7)
    if view.get('reported',{}).get('scope')=='current-Native-Decky-sidecar-failure-observations':view['reported']['scope']='current-Native-Coordinator-staged-commit-failure-observations'
    for row in view['buckets']:
        if row.get('stage') in DECKY_LOG_FAILURE_STAGES:row['stage']=COORDINATOR_COMMIT_FAILURE_STAGES[DECKY_LOG_FAILURE_STAGES.index(row['stage'])]
    if issues:
        view['fieldIssues'].extend(issues)
        if view['state']=='reported':view['state']='reported-with-issues'
    if 'readingRules' in view:view['readingRules']=['commit failures can also contribute original coordinator summary counts;never sum them as unique rows',
        'checked WriteFile/close before one replacement has no explicit flush,crash durability or cross-process read-modify-write guarantee',
        'temporary CREATE_NEW failure never authorizes deleting a preexisting collision;cleanup failure does not prove current path presence or absence',
        'new-row-over-budget is a refused diagnostic record,not a hardware action failure;zero is not health/history',
        'reported writer tuple is same receipt only,not independent OS lifetime or loaded-source proof']
    return view


def coordinator_log_failure_evidence(availability, selection, current_pid):
    """Bounded original coordinator read/rewrite observations,not file durability."""
    mapped={};extra=[];sourceRowCount=None
    if isinstance(availability,dict) and 'coordinatorLogFailureEvidence' in availability:
        value=availability['coordinatorLogFailureEvidence']
        if isinstance(value,dict):
            stats=value.get('stats')
            if isinstance(stats,dict):
                for key,wanted in (('existingSourceReadRequiredBeforeRewrite',True),('newFileRequiresObservedAbsence',True),('explicitFlushPerformed',False),('readSnapshotAtomicWithRewrite',False)):
                    if key not in stats or stats[key] is not wanted:extra.append(key+':missing-or-unsupported')
                keys=('schemaVersion','scope','counterLimit','maxBuckets','maxEventPrefixBytes','snapshotAtomicWithLogFiles',
                    'successfulAttemptsCounted','osErrorCodeAvailable','failureCountsAreUniqueRows','zeroProvesCompleteHistory','currentFileHealthProven','buckets')
                stats={key:stats[key] for key in keys if key in stats}
                stats['scope']='current-Native-Decky-sidecar-failure-observations' if stats.get('scope')=='current-Native-Coordinator-read-rewrite-failure-observations' else None
                if isinstance(stats.get('buckets'),list):
                    rows=stats['buckets'];sourceRowCount=len(rows);mappedRows=[]
                    for row in rows[:7]:
                        if isinstance(row,dict):
                            keys=('stage','observedFailures','counterLimitReached','firstEventKnown','firstEvent','firstEventTruncated','firstEventAsciiSanitized')
                            row={key:row[key] for key in keys if key in row};stage=row.get('stage')
                            row['stage']=DECKY_LOG_FAILURE_STAGES[COORDINATOR_LOG_FAILURE_STAGES.index(stage)] if isinstance(stage,str) and stage in COORDINATOR_LOG_FAILURE_STAGES else None
                        mappedRows.append(row)
                    # Retain the original length for the shared bounded validator without copying arbitrary rows.
                    if len(rows)>7:extra.append('bucket-count-not-seven');extra.append('sourceRowsNotInspected:'+str(len(rows)-7))
                    stats['buckets']=mappedRows
            value={key:value[key] for key in ('writerPid','writerProcessCreation100ns') if key in value}|{'stats':stats}
        mapped['deckyLogFailureEvidence']=value
    view=decky_log_failure_evidence(mapped,selection,current_pid)
    view['scope']='bounded-Coordinator-read-rewrite-failure-observations';view['maxObservedStageKinds']=7
    if sourceRowCount is not None and selection.get('status')=='selected':view['sourceRowsObserved']=sourceRowCount;view['sourceRowsNotInspected']=max(0,sourceRowCount-7)
    if view.get('reported',{}).get('scope')=='current-Native-Decky-sidecar-failure-observations':view['reported']['scope']='current-Native-Coordinator-read-rewrite-failure-observations'
    for row in view['buckets']:
        stage=row.get('stage')
        if stage in DECKY_LOG_FAILURE_STAGES:row['stage']=COORDINATOR_LOG_FAILURE_STAGES[DECKY_LOG_FAILURE_STAGES.index(stage)]
    view['separateCommitReceiptRequired']=False;view['completeCommitInferred']=False
    if isinstance(availability,dict) and isinstance(availability.get('coordinatorLogFailureEvidence'),dict):
        originalStats=availability['coordinatorLogFailureEvidence'].get('stats')
        if isinstance(originalStats,dict) and 'rewriteBackend' in originalStats:
            if originalStats['rewriteBackend']=='same-directory-staged-transient':
                view['reported']['rewriteBackend']='same-directory-staged-transient';view['separateCommitReceiptRequired']=True
            else:extra.append('rewriteBackend:unsupported')
    if extra:
        view['fieldIssues'].extend(extra)
        if view['state']=='reported':view['state']='reported-with-issues'
    if 'readingRules' in view:view['readingRules']=['seven coordinator-owned preparation/source/rewrite failure stages;not Native recovery or unique lost rows',
        'source read exception can include allocation during retention;no OS code or IO cause inferred',
        'observed absence is point-in-time;no atomic source-to-rewrite or concurrent writer protection',
        ('staged commit has its own required failure view;source summary does not prove close/promotion/cleanup or health' if view['separateCommitReceiptRequired'] else 'legacy rewrite may truncate before a write failure;destructor flush/close and rotation internals unobserved;zero is not health/history'),
        'reported PID and decimal birth are same receipt only,not independent OS or loaded-source proof']
    return view


def fan_detail_log_failure_evidence(availability, selection, current_pid):
    """Original fan-api append only;the shared flush slots have no observation."""
    mapped={};extra=[]
    if isinstance(availability,dict) and 'fanDetailLogFailureEvidence' in availability:
        value=availability['fanDetailLogFailureEvidence']
        if isinstance(value,dict):
            stats=value.get('stats')
            if isinstance(stats,dict):
                flags=('explicitFlushPerformed','openBoundaryRepairPerformed')
                for key in flags:
                    if key not in stats or stats[key] is not False:extra.append(key+':missing-or-unsupported')
                keys=('schemaVersion','scope','counterLimit','maxBuckets','maxEventPrefixBytes','snapshotAtomicWithLogFiles',
                    'successfulAttemptsCounted','osErrorCodeAvailable','failureCountsAreUniqueRows','zeroProvesCompleteHistory','currentFileHealthProven','buckets')
                stats={key:stats[key] for key in keys if key in stats}
                stats['scope']='current-Native-Decky-sidecar-failure-observations' if stats.get('scope')=='current-Native-Fan-api-detail-failure-observations' else None
            value={key:value[key] for key in ('writerPid','writerProcessCreation100ns') if key in value}|{'stats':stats}
        mapped['deckyLogFailureEvidence']=value
    view=decky_log_failure_evidence(mapped,selection,current_pid)
    view['scope']='bounded-Fan-api-detail-failure-observations';view['maxObservedStageKinds']=5
    if view.get('reported',{}).get('scope')=='current-Native-Decky-sidecar-failure-observations':view['reported']['scope']='current-Native-Fan-api-detail-failure-observations'
    for bucket in view['buckets']:
        if bucket.get('stage') in ('flush-failure','flush-exception'):
            if bucket.get('consistent') and bucket['reported'].get('observedFailures')==0:
                bucket['state']='not-observed';bucket['reported']={'observationPerformed':False}
            else:extra.append('unobserved-flush-bucket-not-empty-or-invalid')
    if extra:
        view['fieldIssues'].extend(extra)
        if view['state']=='reported':view['state']='reported-with-issues'
    if 'readingRules' in view:
        view['reported']['explicitFlushPerformed']=False if not extra else None
        view['reported']['openBoundaryRepairPerformed']=False if not extra else None
        view['readingRules'].append('only five preparation/open/write stages are observed;flush slots are unobserved,not measured zero;original destructor flush/close and coordinator rewrite are outside scope')
    return view


def fan_lifecycle_log_failure_evidence(availability, selection, current_pid):
    """Same fixed seven-bucket contract;Fan-owned storage stays separate."""
    mapped={}
    if isinstance(availability,dict) and 'fanLifecycleLogFailureEvidence' in availability:
        value=availability['fanLifecycleLogFailureEvidence']
        if isinstance(value,dict):
            stats=value.get('stats')
            if isinstance(stats,dict):
                keys=('schemaVersion','scope','counterLimit','maxBuckets','maxEventPrefixBytes','snapshotAtomicWithLogFiles',
                    'successfulAttemptsCounted','osErrorCodeAvailable','failureCountsAreUniqueRows','zeroProvesCompleteHistory','currentFileHealthProven','buckets')
                stats={key:stats[key] for key in keys if key in stats}
                stats['scope']='current-Native-Decky-sidecar-failure-observations' if stats.get('scope')=='current-Native-Fan-lifecycle-failure-observations' else None
            value={key:value[key] for key in ('writerPid','writerProcessCreation100ns') if key in value}|{'stats':stats}
        mapped['deckyLogFailureEvidence']=value
    view=decky_log_failure_evidence(mapped,selection,current_pid)
    view['scope']='bounded-Fan-lifecycle-failure-observations'
    if view.get('reported',{}).get('scope')=='current-Native-Decky-sidecar-failure-observations':view['reported']['scope']='current-Native-Fan-lifecycle-failure-observations'
    if 'readingRules' in view:view['readingRules'].append('Fan storage is independent;by-value arguments before writer entry,destructor close,rotation internals and Fan-detail are unobserved')
    return view


def buffered_native_sink_evidence(availability, selection, current_pid):
    """Fixed in-process buffer slots;never a successful-write certificate."""
    limit = 9007199254740991
    result = {'schemaVersion': 1, 'scope': 'bounded-current-Native-buffer-slot-observations;not-file-delivery-proof',
        'receipt': 'log-export-availability.json', 'receiptSelectionStatus': selection['status'],
        'archiveEntryOccurrence': selection['selectedOccurrence'], 'wrapperState': 'receipt-unavailable',
        'reported': {}, 'fieldIssues': [], 'writerPid': None, 'writerProcessCreation100ns': None,
        'writerLifetimeKnown': False, 'currentPidMatch': None, 'sinks': [],
        'maxListedSinks': 5, 'sourceRowsObserved': None, 'unassignedSourceRows': None,
        'nonObjectSourceRows': None, 'rowCountsAcrossSinksSummed': False,
        'fileWriteOrDurabilityProven': False, 'resourceReturnProven': False,
        'manifestSuppliesWriterBirth': False, 'zeroPendingOrGoodStreamProvesDelivery': False,
        'readingRules': [
            'failureObservations records observed failures,not unique OS errors,lost rows or a complete session',
            'pendingRows=0 and streamGood=true can follow failed flush/close;never erase earlier first/last failure evidence',
            'first/last belong to this slot in this Native process;not adjacent files or global earliest/latest time',
            'tickKnown=false and tick=null is unknown;tickKnown=true and tick=0 is a real reported boot tick',
            'counterSaturated means a lower-bound count;last witness still advances without a larger count',
            'slot snapshots are not atomic across sinks or staging;never add their counts into a whole-export outcome',
            'exact known file slots only;duplicate slots stay ambiguous,unknown names are counted without selecting a winner',
            'missing/unrecognized/invalid receipt fields stay UNKNOWN;no writer birth,current lifetime or errno backfill']}
    if selection['status'] != 'selected':
        return result
    value = availability.get('bufferedNativeLogEvidence')
    state = ('missing' if 'bufferedNativeLogEvidence' not in availability else 'null' if value is None else
             'object' if isinstance(value, dict) else 'invalid-type')
    result['wrapperState'] = state
    if state != 'object':
        return result
    known = (type(value.get('schemaVersion')) is int and value['schemaVersion'] == 1 and
        value.get('evidenceScope') == 'current-Native-owned-buffer-slots;not-file-write-or-whole-history-proof')
    fields = ('schemaVersion', 'evidenceScope', 'counterLimit', 'snapshotAtomicAcrossSinks', 'fileWriteProof')
    issues = result['fieldIssues']
    for key in fields:
        if key not in value:
            issues.append(key + ':missing');continue
        part = value[key]
        valid = (type(part) is int and part == 1 if key == 'schemaVersion' else
                 isinstance(part, str) and len(part) <= 256 if key == 'evidenceScope' else
                 type(part) is int and part == limit if key == 'counterLimit' else part is False)
        if valid:
            result['reported'][key] = part
        else:
            issues.append(key + ':unsupported-type-or-value')
    writer = value.get('writerPid')
    if type(writer) is int and 0 < writer < (1 << 32):
        result['writerPid'] = str(writer)
        result['currentPidMatch'] = None if current_pid is None else str(writer) == str(current_pid)
    else:
        issues.append('writerPid:missing-or-invalid')
    if not known:
        result['wrapperState'] = 'unsupported';return result
    if issues:
        result['wrapperState'] = 'invalid-fields';return result
    rows = value.get('sinks')
    if not isinstance(rows, list):
        result['wrapperState'] = ('missing-sinks' if 'sinks' not in value else 'null-sinks' if rows is None else 'invalid-sinks')
        return result
    result['wrapperState'] = 'recognized'
    matches = {name: [] for name in BUFFERED_NATIVE_SINKS + EXTRA_BUFFERED_NATIVE_SINKS}
    counts = collections.Counter()
    unassigned = nonobject = 0
    for index, row in enumerate(rows):
        name = row.get('file') if isinstance(row, dict) else None
        if isinstance(name, str) and name in matches:
            counts[name] += 1
            if not matches[name]:
                matches[name].append((index, row))
        else:
            unassigned += 1;nonobject += not isinstance(row, dict)
    result.update(sourceRowsObserved=len(rows), unassignedSourceRows=unassigned, nonObjectSourceRows=nonobject)
    reported_slots = BUFFERED_NATIVE_SINKS + tuple(name for name in EXTRA_BUFFERED_NATIVE_SINKS if counts[name])
    result['maxListedSinks'] = len(reported_slots)
    def uint(value, bits=64):
        return type(value) is int and 0 <= value < (1 << bits)
    def witness(value):
        item = {'state': 'missing-or-null' if value is None else 'invalid-type', 'reported': {}, 'fieldIssues': []}
        if not isinstance(value, dict):
            return item
        selected, errors = item['reported'], item['fieldIssues']
        for key in ('stage', 'tickKnown', 'tick', 'bufferedRowsAtObservation', 'errorCodeObserved', 'errorCode'):
            if key not in value:
                errors.append(key + ':missing');continue
            part = value[key]
            valid = (isinstance(part, str) and 0 < len(part) <= 256 if key == 'stage' else
                type(part) is bool if key in ('tickKnown', 'errorCodeObserved') else
                part is None or uint(part) if key == 'tick' else uint(part, 32) if key == 'bufferedRowsAtObservation' else
                part is None or type(part) is int and -(1 << 31) <= part < (1 << 31))
            if valid:
                selected[key] = part
            else:
                errors.append(key + ':invalid-type-or-range')
        if not errors:
            if selected['tickKnown'] != (selected['tick'] is not None):
                errors.append('tick-known-value-disagree')
            if selected['errorCodeObserved'] != (selected['errorCode'] is not None and selected['errorCode'] != 0):
                errors.append('error-code-known-value-disagree')
            if not selected['errorCodeObserved'] and selected['errorCode'] is not None:
                errors.append('unobserved-error-code-not-null')
        item['state'] = ('invalid-fields' if errors else 'reported-failure' if
            selected['stage'] in BUFFERED_NATIVE_FAILURE_STAGES else 'unsupported-stage')
        return item
    for name in reported_slots:
        count = counts[name]
        item = {'file': name, 'matchingSourceRows': count, 'sourceRowIndex': None,
            'selectionStatus': 'missing-slot' if not count else 'ambiguous-slot' if count > 1 else 'selected',
            'state': 'unknown', 'reported': {}, 'first': None, 'last': None, 'fieldIssues': []}
        result['sinks'].append(item)
        if count != 1:
            continue
        index, row = matches[name][0];item['sourceRowIndex'] = index
        selected, errors = item['reported'], item['fieldIssues']
        for key in ('failureObservations', 'counterSaturated', 'pendingRows', 'streamOpen', 'streamGood'):
            if key not in row:
                errors.append(key + ':missing');continue
            part = row[key]
            valid = (type(part) is int and 0 <= part <= limit if key == 'failureObservations' else
                uint(part, 32) if key == 'pendingRows' else type(part) is bool)
            if valid:
                selected[key] = part
            else:
                errors.append(key + ':invalid-type-or-range')
        for key in ('first', 'last'):
            if key not in row:
                errors.append(key + ':missing')
            item[key] = witness(row.get(key))
        if 'failureObservations' in selected:
            count = selected['failureObservations']
            if 'counterSaturated' in selected and selected['counterSaturated'] != (count == limit):
                errors.append('counter-saturation-disagrees')
            if count == 0:
                if any(key not in row or row[key] is not None for key in ('first', 'last')):
                    errors.append('zero-count-has-failure-witness-or-missing-slot')
            elif any(item[key]['state'] != 'reported-failure' for key in ('first', 'last')):
                errors.append('positive-count-without-supported-first-last')
            elif count == 1 and item['first']['reported'] != item['last']['reported']:
                errors.append('one-count-first-last-disagree')
        item['state'] = ('invalid-fields' if errors else 'reported-failures-observed'
            if selected['failureObservations'] > 0 else 'reported-no-failures-observed-in-slot')
    return result


def facts_rejected_offer_evidence(parent, producer_recognized):
    """At most two reported rejected descriptors;never reconstruct missing types."""
    result = {'schemaVersion': 1, 'state': 'missing', 'witnesses': [], 'maxListedWitnesses': 2,
        'maxPrefixBytes': 2048, 'fieldIssues': [], 'physicalLossOrUniqueTypesInferred': False,
        'counterSnapshotAtomic': False, 'completeSourceCoverageProven': False,
        'writerLifetimeProven': False, 'missingWitnessMeans': 'UNKNOWN;not-zero-drops'}
    if 'rejectedOfferEvidence' not in parent:
        return result
    source = parent['rejectedOfferEvidence']
    if source is None:
        result['state'] = 'null';return result
    if not isinstance(source, dict):
        result['state'] = 'invalid-type';return result
    if not producer_recognized:
        result['state'] = 'unsupported-producer';return result
    if type(source.get('schema')) is not int or source['schema'] != 1:
        result['state'] = 'unsupported-or-missing-schema';return result
    issues = result['fieldIssues']
    for key, expected in (('maximumWitnesses', 2), ('maximumPrefixBytes', 2048)):
        if type(source.get(key)) is not int or source[key] != expected:issues.append(key + ':invalid')
    for key in ('sampleRetained', 'snapshotAtomicWithCounters', 'attemptTickKnown', 'writerLifetimeKnown', 'historyComplete'):
        if source.get(key) is not False:issues.append(key + ':not-explicit-false')
    if 'attemptTickMs' not in source or source['attemptTickMs'] is not None:issues.append('attemptTickMs:not-explicit-null')
    rows = source.get('witnesses')
    if not isinstance(rows, list):
        result['state'] = 'invalid-fields';issues.append('witnesses:missing-or-invalid');return result
    result['sourceWitnessRows'] = len(rows);result['witnessRowsNotInspected'] = max(0, len(rows) - 2)
    if len(rows) != 2:issues.append('witnesses:count-not-two')
    for index, row in enumerate(rows[:2]):
        item = {'index': index, 'state': 'invalid-fields', 'fieldIssues': []};result['witnesses'].append(item)
        if not isinstance(row, dict):item['fieldIssues'].append('row:not-object');continue
        reason = ('busy', 'capacity')[index]
        if row.get('reason') != reason:item['fieldIssues'].append('reason:unexpected-position')
        else:item['reason'] = reason
        state = row.get('state')
        if state not in ('not-observed', 'publishing', 'retained', 'descriptor-serialization-failed'):
            item['fieldIssues'].append('state:unsupported');continue
        item['reportedState'] = state
        descriptor_keys = ('descriptorEncoding', 'descriptorBytes', 'prefixBytes', 'prefixHex', 'descriptorComplete')
        if state != 'retained':
            if any(key in row for key in descriptor_keys):item['fieldIssues'].append('non-retained-has-descriptor')
            item['state'] = 'invalid-fields' if item['fieldIssues'] else state
            continue
        size, count, text, complete = (row.get(k) for k in ('descriptorBytes', 'prefixBytes', 'prefixHex', 'descriptorComplete'))
        valid_size = type(size) is int and 0 < size < (1 << 64)
        valid_count = type(count) is int and 0 < count <= 2048
        valid_hex = valid_count and isinstance(text, str) and len(text) == 2 * count and re.fullmatch('[0-9a-f]+', text)
        if not valid_size:item['fieldIssues'].append('descriptorBytes:invalid')
        if not valid_count:item['fieldIssues'].append('prefixBytes:invalid')
        if not valid_hex:item['fieldIssues'].append('prefixHex:invalid')
        if type(complete) is not bool:item['fieldIssues'].append('descriptorComplete:invalid')
        if row.get('descriptorEncoding') != 'JSON-UTF8-prefix-as-hex':item['fieldIssues'].append('descriptorEncoding:unsupported')
        if valid_size and valid_count and (count != min(size, 2048) or type(complete) is bool and complete != (size == count)):
            item['fieldIssues'].append('descriptor-length-or-completeness-disagrees')
        if item['fieldIssues']:continue
        item.update(descriptorBytes=size, prefixBytes=count, prefixHex=text, descriptorComplete=complete)
        if complete:
            try:
                value = evidence_json_loads(bytes.fromhex(text).decode('utf8'))
                if not isinstance(value, dict) or set(value) != {'model', 'identity', 'category'} or not isinstance(value['category'], str):
                    raise ValueError('descriptor shape')
                item['descriptor'] = value;item['state'] = 'retained-complete-descriptor'
            except (ValueError, TypeError, UnicodeError, RecursionError):item['fieldIssues'].append('descriptor:uninterpretable')
        else:item['state'] = 'retained-prefix-only'
    result['state'] = 'invalid-fields' if issues or any(item['fieldIssues'] for item in result['witnesses']) else 'reported'
    return result


def signals_rejected_type_evidence(parent, producer_recognized):
    """Two fixed POD type witnesses;rejected attempts are not physical input loss."""
    result = {'schemaVersion': 1, 'state': 'missing', 'witnesses': [], 'maxListedWitnesses': 2,
        'fieldIssues': [], 'counterSnapshotAtomic': False, 'completeSourceCoverageProven': False,
        'physicalLossOrUniqueTypesInferred': False, 'writerLifetimeProven': False,
        'missingWitnessMeans': 'UNKNOWN;not-zero-drops'}
    if 'rejectedTypeEvidence' not in parent:
        return result
    source = parent['rejectedTypeEvidence']
    if source is None:
        result['state'] = 'null';return result
    if not isinstance(source, dict):
        result['state'] = 'invalid-type';return result
    if not producer_recognized:
        result['state'] = 'unsupported-producer';return result
    if type(source.get('schema')) is not int or source['schema'] != 1:
        result['state'] = 'unsupported-or-missing-schema';return result
    issues = result['fieldIssues']
    if type(source.get('maximumWitnesses')) is not int or source['maximumWitnesses'] != 2:
        issues.append('maximumWitnesses:invalid')
    for key in ('rawReportRetained', 'snapshotAtomicWithCounters', 'attemptTickKnown', 'writerLifetimeKnown', 'historyComplete'):
        if source.get(key) is not False:issues.append(key + ':not-explicit-false')
    rows = source.get('witnesses')
    if not isinstance(rows, list):
        result['state'] = 'invalid-fields';issues.append('witnesses:missing-or-invalid');return result
    result['sourceWitnessRows'] = len(rows);result['witnessRowsNotInspected'] = max(0, len(rows) - 2)
    if len(rows) != 2:issues.append('witnesses:count-not-two')
    fields = ('source', 'profile', 'command', 'code', 'shape', 'canonicalMask', 'reportId')
    for index, row in enumerate(rows[:2]):
        item = {'index': index, 'state': 'invalid-fields', 'fieldIssues': []};result['witnesses'].append(item)
        errors = item['fieldIssues']
        if not isinstance(row, dict):errors.append('row:not-object');continue
        reason = ('busy', 'capacity')[index]
        if row.get('reason') != reason:errors.append('reason:unexpected-position')
        else:item['reason'] = reason
        state = row.get('state')
        if state not in ('not-observed', 'publishing', 'retained'):
            errors.append('state:unsupported');continue
        item['reportedState'] = state
        if state != 'retained':
            if 'type' in row:errors.append('non-retained-has-type')
        else:
            value = row.get('type')
            if not isinstance(value, dict):errors.append('type:not-object')
            else:
                reported = {}
                for key in fields:
                    part = value.get(key);bits = 64 if key == 'canonicalMask' else 32
                    if type(part) is not int or not 0 <= part < (1 << bits):errors.append(key + ':invalid-uint')
                    else:reported[key] = part
                item['reportedType'] = reported
                item['unprojectedTypeFieldCount'] = len(value) - sum(key in value for key in fields)
        if not errors:item['state'] = state
    result['state'] = 'invalid-fields' if issues or any(q['fieldIssues'] for q in result['witnesses']) else 'reported'
    return result


JOURNAL_FAILURE_STAGES = ('local-lock-busy','retry-backoff','named-mutex-create','named-mutex-wait',
    'file-exists-query','file-size-query','file-time-query','file-open-read','file-read','file-open-append',
    'file-write-flush','file-close','exception','committed-file-size-query','committed-file-time-query')


JOURNAL_BUDGET_REASONS = ('cachedFullPath','observedFileOverMaxBytes','recordCountLimit','recordBytesLimit','appendBytesLimit')


def journal_budget_evidence(source, recognized):
    """Five fixed omission-branch counters;never reconstruct legacy causes."""
    value=source.get('budgetEvidence')
    result={'state':'missing' if 'budgetEvidence' not in source else 'null' if value is None else 'invalid-type',
        'reported':{},'fieldIssues':[],'maxListedReasons':5,'positiveReasons':[],
        'counterLimitReachedReasons':[],'reasonCountersSummed':False,
        'uniqueLostRecordsInferred':False,'legacyCausesBackfilled':False,'fileDeliveryProven':False,
        'readingRules':['cachedFullPath counts later observations at an existing path latch;it does not identify a new capacity event or the original latch cause',
            'recordCountLimit,recordBytesLimit and appendBytesLimit may overlap in one omission;never sum them or equate them with budgetOmissions',
            'recordBytesLimit alone does not latch the journal;later smaller records can still be accepted under the original policy',
            'counts belong to one Journal instance across its paths;no path,writer lifetime,missing payload or old-history cause is reconstructed',
            'zero counts or writeFailures=0 do not prove complete permanent history;absent or invalid metadata stays unknown']}
    if not recognized:result['state']='producer-unrecognized';return result
    if not isinstance(value,dict):return result
    if type(value.get('schema')) is not int or value['schema']!=1:result['state']='unsupported';return result
    issues=result['fieldIssues']
    checks=(('schema',lambda v:type(v) is int and v==1),
        ('scope',lambda v:v=='current-Journal-instance-budget-branch-observations'),
        ('counterLimit',lambda v:type(v) is int and v==(1<<64)-1),
        ('counterPolicy',lambda v:v=='saturating;overlapping-limits-not-summed'),
        ('snapshotAtomicWithBudgetOmissions',lambda v:v is True),
        ('observationsAreUniqueRecords',lambda v:v is False),('durableHistoryProven',lambda v:v is False))
    for key,valid in checks:
        if key not in value:issues.append(key+':missing')
        elif valid(value[key]):result['reported'][key]=value[key]
        else:issues.append(key+':invalid-type-or-value')
    counts=value.get('observations')
    if not isinstance(counts,dict):issues.append('observations:missing-or-invalid')
    else:
        reported={};result['reported']['observations']=reported
        for key in JOURNAL_BUDGET_REASONS:
            count=counts.get(key)
            if key not in counts:issues.append(key+':missing')
            elif type(count) is not int or not 0<=count<(1<<64):issues.append(key+':invalid-uint64')
            else:reported[key]=count
        result['unprojectedReasonFieldCount']=len(counts)-sum(key in counts for key in JOURNAL_BUDGET_REASONS)
    result['unprojectedFieldCount']=len(value)-sum(key in value for key,_ in checks)-int('observations' in value)
    result['state']='invalid-fields' if issues else 'reported'
    if not issues:
        result['positiveReasons']=[key for key in JOURNAL_BUDGET_REASONS if counts[key]>0]
        result['counterLimitReachedReasons']=[key for key in JOURNAL_BUDGET_REASONS if counts[key]==(1<<64)-1]
    return result


def journal_failure_evidence(source, recognized):
    value=source.get('failureEvidence')
    result={'state':'missing' if 'failureEvidence' not in source else 'null' if value is None else 'invalid-type',
        'reported':{},'fieldIssues':[],'stages':[],'maxListedStages':15,
        'legacyWriteFailuresReconciled':False,'fullFailureHistoryProven':False,'zeroObservationsProvesHealth':False,
        'readingRules':['branch observations are not unique failed operations;backoff and lock busy are deferrals,not extra write errors',
            'only first successful claim per fixed stage is retained;later error kinds and all timestamps remain unknown',
            'stream failures supply no GetLastError;Win32 and std::error_code retain different numeric domains',
            'file-read also covers disagreement with the earlier size observation;IO failure versus concurrent source change remains unknown',
            'committed metadata query failure follows reported saved;it does not revoke or certify the write',
            'counter and first-claim snapshots are not atomic;positive count with absent/publishing first claim remains explicit']}
    if not recognized:result['state']='producer-unrecognized';return result
    if not isinstance(value,dict):return result
    if type(value.get('schema')) is not int or value['schema']!=1:result['state']='unsupported';return result
    issues=result['fieldIssues']
    for key,valid in (
        ('scope',lambda v:v=='current-Journal-instance-original-branches;first-successful-claim-per-stage'),
        ('maximumStages',lambda v:type(v) is int and v==15),('counterLimit',lambda v:type(v) is int and v==(1<<64)-1),
        *[(k,lambda v:v is False) for k in ('snapshotAtomicWithCounters','observationsAreUniqueFailedOperations','allErrorKindsRetained','streamLastErrorQueried','durableWriteProven')]):
        if key not in value:issues.append(key+':missing')
        elif valid(value[key]):result['reported'][key]=value[key]
        else:issues.append(key+':invalid-type-or-value')
    rows=value.get('stages')
    if not isinstance(rows,list):result['state']='invalid-fields';issues.append('stages:missing-or-invalid');return result
    result['sourceRowsObserved']=len(rows);matches={name:[] for name in JOURNAL_FAILURE_STAGES};counts=collections.Counter();unknown=0
    for row in rows:
        name=row.get('stage') if isinstance(row,dict) else None
        if isinstance(name,str) and name in matches:
            counts[name]+=1
            if not matches[name]:matches[name].append(row)
        else:unknown+=1
    result['unrecognizedStageRows']=unknown
    if unknown:issues.append('unrecognized-stage-rows')
    for name in JOURNAL_FAILURE_STAGES:
        item={'stage':name,'state':'missing','reported':{},'fieldIssues':[]};result['stages'].append(item)
        if counts[name]!=1:
            if counts[name]>1:item['state']='ambiguous-repeated-stage'
            continue
        row=matches[name][0];problems=item['fieldIssues'];n=row.get('observations');state=row.get('firstState')
        if type(n) is not int or not 0<=n<(1<<64):problems.append('observations:missing-or-invalid')
        else:item['reported']['observations']=n
        if not isinstance(state,str) or state not in ('not-observed','publishing','retained'):problems.append('firstState:missing-or-invalid')
        else:item['reported']['firstState']=state
        if state!='retained':
            if 'first' in row:problems.append('unretained-first-present')
            item['state']='invalid-fields' if problems else 'reported-'+state;continue
        first=row.get('first');item['state']='invalid-fields'
        if not isinstance(first,dict):problems.append('first:missing-or-invalid');continue
        if set(first)!={'codeKnown','codeDomain','code','waitStatusKnown','waitStatus','exceptionKind'}:problems.append('first:unsupported-shape')
        known,domain,code=first.get('codeKnown'),first.get('codeDomain'),first.get('code')
        wait_known,wait,kind=first.get('waitStatusKnown'),first.get('waitStatus'),first.get('exceptionKind')
        fs_stage=name in ('file-exists-query','file-size-query','file-time-query','committed-file-size-query','committed-file-time-query')
        if type(known) is not bool or type(wait_known) is not bool:problems.append('known-flags:missing-or-invalid')
        elif known:
            valid=(domain=='win32-last-error' and name in ('named-mutex-create','named-mutex-wait') and type(code) is int and 0<=code<(1<<32) or
                isinstance(domain,str) and domain in ('std-error-code:generic','std-error-code:system','std-error-code:other-category') and fs_stage and type(code) is int and -(1<<31)<=code<(1<<31))
            if not valid:problems.append('code-domain-stage-or-range-disagrees')
        elif domain is not None or code is not None:problems.append('unknown-code-has-value')
        if wait_known is True:
            if name!='named-mutex-wait' or type(wait) is not int or not 0<=wait<(1<<32):problems.append('wait-status-stage-or-range-disagrees')
            elif wait in (0,128):problems.append('successful-wait-marked-deferred')
            elif (wait==(1<<32)-1)!=(known is True):problems.append('failed-wait-code-disagrees')
        elif wait is not None or name=='named-mutex-wait':problems.append('wait-status-unknown-or-unexpected')
        if name=='named-mutex-create' and known is not True:problems.append('create-error-missing')
        if fs_stage and known is not True:problems.append('filesystem-error-missing')
        if name=='exception':
            if not isinstance(kind,str) or kind not in ('allocation-exception','standard-exception','unknown-exception') or known is not False:problems.append('exception-fields-disagree')
        elif kind is not None:problems.append('unexpected-exception-kind')
        if not problems:
            item['state']='reported-first';item['reported']['first']={k:first[k] for k in ('codeKnown','codeDomain','code','waitStatusKnown','waitStatus','exceptionKind')}
    result['state']='reported-with-issues' if issues or any(x['state'] in ('missing','ambiguous-repeated-stage','invalid-fields') for x in result['stages']) else 'reported'
    return result


def adaptation_store_coverage_evidence(availability, selection):
    """Fixed adaptation-store counters at export;no first-witness delivery proof."""
    result = {'schemaVersion': 1, 'scope': 'bounded-adaptation-store-status-at-export;not-complete-input-or-durable-history',
        'receipt': 'log-export-availability.json', 'receiptSelectionStatus': selection['status'],
        'archiveEntryOccurrence': selection['selectedOccurrence'], 'families': [], 'maxListedFamilies': 2,
        'snapshotAtomicAcrossStores': False, 'completeHistoryProven': False,
        'writerLifetimeProven': False, 'oldJournalRowsAttributedToExporter': False,
        'physicalInputLossCountInferred': False, 'zeroFailuresProvesCoverage': False,
        'readingRules': [
            'facts/signals/journal are separate snapshots;never add counters across stores or infer a single atomic cut',
            'busy/contention/capacity/boundedDrops count rejected offer attempts,not unique lost types or physical presses',
            'legacy boundedDrops without split counters preserves an unknown cause;no split is backfilled',
            'pending zero or writeFailures zero cannot prove complete collection,persistence,or successful physical action',
            'budgetFull and budgetOmissions are independent of IO errors;absence of a journal row can remain unknown',
            'journal bytes/unique records are last observed cached values;not a live source or packed-byte certificate',
            'unsupported family identifiers retain bounded typed fields without interpreting their counters',
            'missing,null,invalid and recorded zero stay distinct;raw metadata remains in the archive']}
    if selection['status'] != 'selected':
        return result
    def shape(parent, key):
        value = parent.get(key)
        return ('missing' if key not in parent else 'null' if value is None else
                'object' if isinstance(value, dict) else 'invalid-type'), value
    def project(value, spec):
        reported, states = {}, {}
        for key, kind in spec.items():
            if key not in value:
                states[key] = 'missing';continue
            part = value[key]
            if part is None:
                reported[key] = None;states[key] = 'null';continue
            valid = (type(part) is int and 0 <= part < (1 << 64) if kind == 'uint' else
                     type(part) is bool if kind == 'bool' else isinstance(part, str) and len(part) <= 512)
            states[key] = 'recorded' if valid else 'invalid-type-or-range'
            if valid:
                reported[key] = part
        return {'reported': reported, 'fieldStates': states,
                'unprojectedFieldCount': len(value) - sum(key in value for key in spec)}
    def known(view, key):
        return view['fieldStates'].get(key) == 'recorded'
    facts = dict.fromkeys(('capacity', 'knownFacts', 'pendingFacts', 'boundedDrops', 'busyDrops', 'capacityDrops'), 'uint')
    facts.update(eviction='bool', dropScope='text')
    signals = dict.fromkeys(('capacity', 'knownTypes', 'pendingTypes', 'contentionDrops', 'capacityDrops'), 'uint')
    signals.update(eviction='bool', scope='text')
    journal = dict.fromkeys(('maxBytes', 'maxUniqueRecords', 'maxRecordBytes', 'lastObservedJournalBytes',
        'lastObservedUniqueRecords', 'priorInvalidRows', 'journalReadScans', 'committedThisProcess',
        'writeFailures', 'budgetOmissions', 'retryBackoffMs'), 'uint')
    journal.update(cachePolicy='text', budgetFull='bool', completeHistory='bool', budgetPolicy='text')
    family_specs = (
        ('modelAdaptation', 'model-adaptation.log', 'ymcc.model-adaptation-status/1', ('facts', 'signals', 'journal')),
        ('virtualAdaptation', 'virtual-adaptation.log', None, ('facts', 'journal')))
    for family, filename, schema, sections in family_specs:
        state, value = shape(availability, family)
        view = {'family': family, 'state': state, 'producerRecognized': False, 'stores': []}
        result['families'].append(view)
        if state != 'object':
            continue
        spec = {'file': 'text', 'alwaysOn': 'bool', 'scope': 'text'}
        if schema is not None:
            spec.update(schema='text', detailGateRequired='bool', modelContext='text', uniqueness='text')
        else:
            spec.update(maxBytes='uint', maxRecords='uint')
        view.update(project(value, spec))
        view['unprojectedFieldCount'] -= sum(key in value for key in sections)
        recognized = value.get('file') == filename and (schema is None or value.get('schema') == schema)
        view['producerRecognized'] = recognized
        view['producerState'] = 'recognized' if recognized else 'unsupported-or-unidentified'
        for section in sections:
            state, source = shape(value, section)
            item = {'store': section, 'state': state, 'reportedPositiveGapCounters': [],
                    'queueAtCapacity': None, 'reportedBudgetFull': None, 'fieldIssues': []}
            view['stores'].append(item)
            if state != 'object':
                continue
            item.update(project(source, {'facts': facts, 'signals': signals, 'journal': journal}[section]))
            if section == 'facts':
                item['rejectedOffers'] = facts_rejected_offer_evidence(source, recognized)
                item['unprojectedFieldCount'] -= int('rejectedOfferEvidence' in source)
            if section == 'journal':
                item['failureStages'] = journal_failure_evidence(source, recognized)
                item['unprojectedFieldCount'] -= int('failureEvidence' in source)
                item['budgetReasons'] = journal_budget_evidence(source, recognized)
                item['unprojectedFieldCount'] -= int('budgetEvidence' in source)
            if section == 'signals':
                item['rejectedTypes'] = signals_rejected_type_evidence(source, recognized)
                item['unprojectedFieldCount'] -= int('rejectedTypeEvidence' in source)
            errors, reported = item['fieldIssues'], item['reported']
            if section != 'journal':
                count, pending = ('knownFacts', 'pendingFacts') if section == 'facts' else ('knownTypes', 'pendingTypes')
                if all(known(item, key) for key in (count, 'capacity')):
                    if reported[count] > reported['capacity']:
                        errors.append(count + ':exceeds-capacity')
                    elif recognized:
                        item['queueAtCapacity'] = reported[count] == reported['capacity']
                if all(known(item, key) for key in (count, pending)) and reported[pending] > reported[count]:
                    errors.append(pending + ':exceeds-known')
            elif recognized and known(item, 'budgetFull'):
                item['reportedBudgetFull'] = reported['budgetFull']
            gaps = ('boundedDrops', 'busyDrops', 'contentionDrops', 'capacityDrops',
                    'priorInvalidRows', 'writeFailures', 'budgetOmissions')
            if recognized:
                item['reportedPositiveGapCounters'] = [key for key in gaps if known(item, key) and reported[key] > 0]
            item['fieldState'] = ('partial-or-invalid' if errors or any(
                status != 'recorded' for status in item['fieldStates'].values()) else 'recorded')
    return result


def cost_compaction_member_evidence(filename, rows, metadata, data):
    """Pair only parsed marker IDs within one retained member,never across rotations."""
    kinds = ('log-retention-begin', 'log-retention-end')
    groups = {};marker_rows = invalid_ids = untracked = 0;invalid_refs = []
    def uint(value):return type(value) is int and 0 <= value < (1 << 64)
    def project(row, line, begin):
        result = {'line': line, 'reported': {}, 'fieldIssues': []}
        specs = [('retainedSourceBytes', uint), ('historyComplete', lambda v: v is False)]
        if begin:
            specs += [('writerPid', lambda v: type(v) is int and 0 < v < (1 << 32)),
                ('sourceBytes', uint), ('droppedPrefixBytes', uint),
                ('policy', lambda v: v == 'newest-lines-single-active-20MB' if isinstance(v, str) else False),
                ('sourceEndedWithNewline', lambda v: type(v) is bool),
                ('sourceOrder', lambda v: v == 'retained-file-order;not-process-session-proof' if isinstance(v, str) else False)]
        else:specs += [('partialTailRetained', lambda v: type(v) is bool), ('newRowsFollow', lambda v: v is True)]
        for key, valid in specs:
            if key not in row:result['fieldIssues'].append(key + ':missing')
            elif valid(row[key]):result['reported'][key] = row[key]
            else:result['fieldIssues'].append(key + ':invalid-type-or-value')
        return result
    for line, row in rows:
        event = row.get('event')
        if event not in kinds:continue
        marker_rows += 1;identifier = row.get('compactionId')
        if not (isinstance(identifier, str) and len(identifier) == 32 and re.fullmatch('[0-9a-f]{32}', identifier)):
            invalid_ids += 1
            if len(invalid_refs) < 4:invalid_refs.append({'line': line, 'event': event, 'reason': 'invalid-compaction-id'})
            continue
        if identifier not in groups:
            if len(groups) >= 16:untracked += 1;continue
            groups[identifier] = {'compactionId': identifier, 'beginCount': 0, 'endCount': 0, 'begin': None, 'end': None}
        item = groups[identifier];key = 'begin' if event == kinds[0] else 'end'
        item[key + 'Count'] += 1
        if item[key] is None:item[key] = project(row, line, key == 'begin')
    wanted = {item[key]['line'] for item in groups.values() for key in ('begin', 'end') if item[key] is not None}
    spans = {};offset = 0;number = 1
    while wanted and offset < len(data):
        newline = data.find(b'\n', offset);stop = len(data) if newline < 0 else newline + 1
        if number in wanted:spans[number] = (offset, stop, newline >= 0);wanted.remove(number)
        offset = stop;number += 1
    for item in groups.values():
        item.update(status='unverified', issues=[], writerPid=None, writerLifetimeKnown=False,
            retainedRangeBytesConsistent=None, observedBetweenMarkerBytes=None, expectedBetweenMarkerBytes=None)
        begin, end = item['begin'], item['end']
        if begin and 'writerPid' in begin['reported']:item['writerPid'] = str(begin['reported']['writerPid'])
        if item['beginCount'] > 1 or item['endCount'] > 1:item['status'] = 'ambiguous-duplicate-marker-id';continue
        if begin is None or end is None:item['status'] = 'missing-begin-in-member' if begin is None else 'missing-end-in-member';continue
        if begin['fieldIssues'] or end['fieldIssues']:item['status'] = 'invalid-marker-fields';continue
        a, b = begin['reported'], end['reported'];issues = item['issues']
        if begin['line'] >= end['line']:issues.append('end-not-after-begin')
        if a['droppedPrefixBytes'] > a['sourceBytes'] or a['retainedSourceBytes'] != a['sourceBytes'] - a['droppedPrefixBytes']:
            issues.append('source-prefix-retained-bytes-disagree')
        if a['retainedSourceBytes'] != b['retainedSourceBytes']:issues.append('begin-end-retained-bytes-disagree')
        separator = a['retainedSourceBytes'] > 0 and not a['sourceEndedWithNewline']
        if b['partialTailRetained'] != separator:issues.append('partial-tail-flags-disagree')
        first, last = spans.get(begin['line']), spans.get(end['line'])
        if first is not None and last is not None and last[0] >= first[1]:
            item['observedBetweenMarkerBytes'] = last[0] - first[1]
            item['expectedBetweenMarkerBytes'] = a['retainedSourceBytes'] + int(separator)
            item['retainedRangeBytesConsistent'] = item['observedBetweenMarkerBytes'] == item['expectedBetweenMarkerBytes']
            item['endMarkerEndsWithNewline'] = last[2]
            if not item['retainedRangeBytesConsistent']:issues.append('retained-range-byte-length-disagrees')
        else:issues.append('marker-byte-range-unavailable')
        item['status'] = 'inconsistent-marker-evidence' if issues else 'consistent-retained-range'
    return {'file': filename, **metadata, 'scope': 'parsed-cost-retention-markers-and-retained-byte-spans-in-this-member',
        'markerRowsObserved': marker_rows, 'invalidIdMarkerRows': invalid_ids, 'invalidIdFirstReferences': invalid_refs,
        'untrackedMarkerRows': untracked, 'maxTrackedCompactionIds': 16, 'selection': 'first16-valid-distinct-ids',
        'compactions': list(groups.values()), 'historyCompleteProven': False, 'physicalInputLossInferred': False,
        'sourceContentOrDurabilityProven': False, 'crossFilePairsMerged': False}


class CostCompactionEvidence:
    def __init__(self):
        self.total = 0;self.prefix = [];self.tail = collections.deque(maxlen=4)
    def observe(self, filename, rows, metadata, data):
        item = cost_compaction_member_evidence(filename, rows, metadata, data);self.total += 1
        if len(self.prefix) < 4:self.prefix.append(item)
        else:self.tail.append(item)
    def evidence(self):
        members = self.prefix + list(self.tail)
        return {'schemaVersion': 1, 'scope': 'offline-retained-cost-log-compaction-markers;not-complete-history-or-physical-input-loss',
            'membersObserved': self.total, 'members': members, 'maxListedMembers': 8,
            'memberSummariesOmitted': self.total - len(members), 'maxTrackedCompactionIdsPerMember': 16,
            'historyCompleteProven': False, 'runtimeWriterCountersInferred': False,
            'readingRules': [
                '20MB applies to the active cost payload only;not total logs or Steam archives',
                'begin/end match only by original compactionId within one member and occurrence;duplicates remain ambiguous',
                'retainedSourceBytes counts copied old bytes;an unterminated retained tail adds one LF before the end marker',
                'matching byte length is self-consistency,not original-content SHA,successful compaction commit,durability or complete source history',
                'missing markers can be retained prefixes/suffixes or observation gaps;never infer source corruption or a failed device action',
                'begin writer PID has no birth;end has no writer identity;manifest and neighboring markers never supply one',
                'bounded summaries omit untracked marker rows and members explicitly;original archive bytes and file quality remain authoritative']}


def cost_retention_evidence(availability, selection, files):
    """Existing discovery policy and retained active bytes;no source/history proof."""
    result = {'schemaVersion': 1, 'scope': 'bounded-cost-retention-and-discovery;not-live-cap-enforcement-or-total-log-budget',
        'receipt': 'log-export-availability.json', 'receiptSelectionStatus': selection['status'],
        'archiveEntryOccurrence': selection['selectedOccurrence'], 'policy': {'state': 'receipt-unavailable'},
        'discovery': {'state': 'receipt-unavailable'}, 'activeEntries': [], 'activeEntriesObserved': 0,
        'activeEntriesNotListed': 0, 'maxListedActiveEntries': 8, 'activeEntryNamesAmbiguous': False,
        'totalLogBudgetInferred': False, 'oldHistoryAbsenceInferred': False,
        'legacyVolumesPackedInferred': False, 'sourceHistoryOrCurrentWriterProven': False,
        'readingRules': [
            'fileMaxBytes declares only the single active cost payload cap;Steam and other logs have no total budget inferred',
            'budgetBytes=0 means legacy cost volumes were not selected;it does not mean no old volumes or anomalies existed',
            'discovery is before snapshot and may be partial;complete-at-discovery is not packed-byte or later source proof',
            'same names in discovery and retained entries are not joined;selected volumes are not proof of successful packing',
            'active bytes describe each retained exact root entry only;duplicates stay separate and no winner is selected',
            'within a declared cap is not a current-writer/source-epoch guarantee;above it is a comparison,not a proved source bug',
            'source names/counters/reasons are bounded observations;missing,null,invalid and unsupported stay distinct']}
    def shape(parent, key):
        value = parent.get(key)
        return ('missing' if key not in parent else 'null' if value is None else
                'object' if isinstance(value, dict) else 'invalid-type'), value
    def project(source, spec):
        reported, states = {}, {}
        for key, kind in spec.items():
            if key not in source:
                states[key] = 'missing';continue
            value = source[key]
            if value is None:
                states[key] = 'null';reported[key] = None;continue
            valid = (type(value) is bool if kind == 'bool' else
                     type(value) is int and 0 <= value < (1 << 64) if kind == 'uint' else
                     type(value) is int and -(1 << 31) <= value < (1 << 31) if kind == 'int32' else
                     isinstance(value, str) and len(value) <= 256)
            states[key] = 'recorded' if valid else 'invalid-type-or-range'
            if valid:
                reported[key] = value
        return {'reported': reported, 'fieldStates': states,
                'unknownFieldCount': len(source) - sum(key in source for key in spec)}
    def known(view, key):
        return view['fieldStates'].get(key) == 'recorded'
    if selection['status'] == 'selected':
        state, policy = shape(availability, 'costLogRetention')
        result['policy'] = {'state': state}
        if state == 'object':
            spec = {'policy': 'text', 'fileMaxBytes': 'uint', 'compressedVolumesEnabled': 'bool',
                'legacyPendingAndRotationsIncluded': 'bool', 'sourceHistoryComplete': 'bool', 'legacyCleanup': 'text'}
            view = project(policy, spec);result['policy'].update(view)
            reported = view['reported']
            current = (all(known(view, key) for key in spec) and
                reported['policy'] == 'single-active-newest-lines' and reported['fileMaxBytes'] == 20000000 and
                reported['compressedVolumesEnabled'] is False and reported['legacyPendingAndRotationsIncluded'] is False and
                reported['sourceHistoryComplete'] is False and reported['legacyCleanup'] == 'Host-owned-one-shot;export-does-not-delete')
            result['policy']['state'] = 'reported-current-single-active-policy' if current else 'unrecognized-or-incomplete-policy'
        state, discovery = shape(availability, 'costArchiveDiscovery')
        result['discovery'] = {'state': state}
        if state == 'object':
            outer = project(discovery, {'status': 'text', 'archivesFound': 'uint', 'errorCode': 'int32', 'atomicWithSnapshot': 'bool'})
            outer['unknownFieldCount'] -= int('exportSelection' in discovery)
            result['discovery'].update(outer)
            state, document = shape(discovery, 'exportSelection')
            view = {'state': state, 'rows': [], 'rowsObserved': None, 'rowsNotListed': None,
                    'maxListedRows': 21, 'issues': [], 'countsByReasonState': {},
                    'schemaRecognitionIsNotFullSemanticValidation': True}
            result['discovery']['exportSelection'] = view
            if state == 'object':
                spec = {key: 'uint' for key in ('schemaVersion', 'budgetBytes', 'selectedSourceBytes', 'knownDiscoveredBytes',
                    'selectedVolumes', 'omittedVolumes', 'sourceSizeErrors')}
                spec.update({key: 'bool' for key in ('knownDiscoveredBytesSaturated', 'allDiscoveredHistorySelected', 'sourceHistoryModified')})
                spec.update({key: 'text' for key in ('policy', 'evidenceScope', 'budgetScope', 'order', 'omittedMeaning')})
                view.update(project(document, spec));reported = view['reported']
                view['unknownFieldCount'] -= int('archives' in document)
                recognized = (type(document.get('schemaVersion')) is int and document['schemaVersion'] == 1 and
                    document.get('policy') == 'recent-whole-cost-volumes-with-explicit-omission-inventory')
                view['state'] = 'recognized-schema' if recognized else 'unsupported-schema'
                view['volumeBudgetState'] = ('reported-zero-budget' if reported['budgetBytes'] == 0 else 'reported-positive-budget') if known(view, 'budgetBytes') else 'unknown'
                rows = document.get('archives')
                view['archivesFieldState'] = ('missing' if 'archives' not in document else
                    'null' if rows is None else 'array' if isinstance(rows, list) else 'invalid-type')
                if isinstance(rows, list):
                    head, tail, witnesses, reasons = [], collections.deque(maxlen=8), {}, collections.Counter()
                    for index, row in enumerate(rows):
                        item = {'sourceRowIndex': index, 'reported': {}, 'fieldStates': {}}
                        if not isinstance(row, dict):
                            category = 'invalid-row'
                        else:
                            item.update(project(row, {'name': 'text', 'sourceSizeKnown': 'bool', 'sourceBytes': 'uint',
                                'includedByBudget': 'bool', 'reason': 'text', 'errorCode': 'int32'}))
                            name = row.get('name')
                            item['recognizedArchiveBasename'] = isinstance(name, str) and bool(COST_ARCHIVE.fullmatch(name))
                            reason = row.get('reason')
                            category = reason if isinstance(reason, str) and reason in (
                                'source-size-error', 'recent-whole-volume-selected', 'ordinary-export-cost-history-byte-budget') else 'other-or-missing-reason'
                        item['reasonState'] = category;reasons[category] += 1
                        ref = index, item
                        if len(head) < 8:
                            head.append(ref)
                        tail.append(ref);witnesses.setdefault(category, ref)
                    listed = dict(head + list(tail) + list(witnesses.values()))
                    view.update(rowsObserved=len(rows), rowsNotListed=len(rows) - len(listed),
                        rows=[listed[key] for key in sorted(listed)], countsByReasonState=dict(reasons))
                    if recognized and known(view, 'selectedVolumes') and known(view, 'omittedVolumes') and reported['selectedVolumes'] + reported['omittedVolumes'] != len(rows):
                        view['issues'].append('selection-counts-disagree-with-retained-source-list')
                    if recognized and known(outer, 'archivesFound') and outer['reported']['archivesFound'] != len(rows):
                        view['issues'].append('discovery-count-disagrees-with-retained-source-list')
                if recognized and known(view, 'budgetBytes') and reported['budgetBytes'] == 0:
                    if any(known(view, key) and reported[key] != 0 for key in ('selectedVolumes', 'selectedSourceBytes')):
                        view['issues'].append('zero-budget-reports-selection')
                if recognized and known(view, 'omittedVolumes') and known(view, 'allDiscoveredHistorySelected'):
                    if reported['allDiscoveredHistorySelected'] != (reported['omittedVolumes'] == 0):
                        view['issues'].append('all-history-selected-flag-disagrees')
    head, tail, count = [], collections.deque(maxlen=4), 0
    for item in files:
        if item.get('file') != 'input-host-cost.log':
            continue
        ref = count, item;count += 1
        if len(head) < 4:
            head.append(ref)
        tail.append(ref)
    result['activeEntriesObserved'] = count;result['activeEntryNamesAmbiguous'] = count > 1
    selected = dict(head + list(tail))
    policy = result['policy'];declared_cap = policy.get('reported', {}).get('fileMaxBytes')
    cap_known = policy['state'] == 'reported-current-single-active-policy' and type(declared_cap) is int and declared_cap > 0
    for index, item in sorted(selected.items()):
        size = item.get('bytes');size_known = type(size) is int and size >= 0
        result['activeEntries'].append({'file': 'input-host-cost.log', 'retainedEntryOrdinal': index,
            'archiveEntryOccurrence': item.get('archiveEntryOccurrence'), 'bytes': size if size_known else None,
            'declaredCapComparison': 'unknown' if not cap_known or not size_known else
                'within-reported-cap' if size <= declared_cap else 'above-reported-cap'})
    result['activeEntriesNotListed'] = count - len(result['activeEntries'])
    return result


class FanHostDiagnosticInstanceEvidence:
    """One carrier only;reported logger IDs are not OS process lifetimes."""
    MAX_INSTANCES=128
    MAX_EVENT_KINDS=32
    MAX_PARENT_CLAIMS=4
    MAX_MEMBERS=8

    def __init__(self):
        self.instances={};self.rows=0;self.unsupported=0;self.invalid=0;self.omitted=0

    @staticmethod
    def _pid(value):
        return str(value) if type(value) is int and 0<value<(1<<32) else None

    @staticmethod
    def _birth(value):
        return str(int(value)) if isinstance(value,str) and 0<len(value)<=20 and value.isascii() and value.isdecimal() and 0<int(value)<(1<<64) else None

    @staticmethod
    def _utc_birth(value):
        if not isinstance(value,str) or len(value)>40:return None
        match=re.fullmatch(r'([0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2})\.([0-9]{7})(?:Z|\+00:00)',value)
        if not match:return None
        try:
            delta=datetime.datetime.strptime(match[1],'%Y-%m-%dT%H:%M:%S')-datetime.datetime(1601,1,1)
            ticks=delta.days*864000000000+delta.seconds*10000000+int(match[2])
            return str(ticks) if 0<ticks<(1<<64) else None
        except ValueError:return None

    @staticmethod
    def _time(value):
        if not isinstance(value,str) or len(value)>40 or not re.fullmatch(r'[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}(?:\.[0-9]{1,7})?(?:Z|\+00:00)',value):return None
        try:datetime.datetime.fromisoformat(value.replace('Z','+00:00'));return value
        except ValueError:return None

    def observe(self,records):
        for line,row in records:
            self.rows+=1
            if not isinstance(row,dict) or type(row.get('schemaVersion')) is not int or row['schemaVersion']!=2 or type(row.get('diagnosticSchemaVersion')) is not int or row['diagnosticSchemaVersion']!=1:
                self.unsupported+=1;continue
            pid=self._pid(row.get('processId'));instance=row.get('hostInstanceId')
            if pid is None or not isinstance(instance,str) or re.fullmatch(r'[0-9a-f]{32}',instance) is None:
                self.invalid+=1;continue
            key=pid,instance
            if key not in self.instances:
                if len(self.instances)>=self.MAX_INSTANCES:self.omitted+=1;continue
                self.instances[key]={'reportedWriterPid':pid,'reportedLoggerInstanceId':instance,'rows':0,
                    'firstLine':line,'lastLine':line,'firstTimestampUtc':self._time(row.get('timestampUtc')),
                    'lastTimestampUtc':None,'eventCounts':{},'unlistedEventRows':0,'runIdStates':collections.Counter(),
                    'startRows':0,'parentClaimStates':collections.Counter(),'parentClaims':{},'unlistedParentClaimRows':0}
            item=self.instances[key];item['rows']+=1;item['lastLine']=line;item['lastTimestampUtc']=self._time(row.get('timestampUtc'))
            label=row.get('runId');item['runIdStates']['matched-derived-label' if label==f'fan-host:{pid}:{instance}' else 'missing' if 'runId' not in row else 'invalid-or-mismatch']+=1
            event=row.get('eventName')
            if isinstance(event,str) and 0<len(event)<=96 and all(32<=ord(ch)<=126 for ch in event):
                if event in item['eventCounts'] or len(item['eventCounts'])<self.MAX_EVENT_KINDS:item['eventCounts'][event]=item['eventCounts'].get(event,0)+1
                else:item['unlistedEventRows']+=1
            else:item['unlistedEventRows']+=1
            if event!='host.instance-started':continue
            item['startRows']+=1;detail=row.get('details')
            if not isinstance(detail,dict):item['parentClaimStates']['missing-or-invalid-details']+=1;continue
            parent=self._pid(detail.get('parentPid'));birth=self._utc_birth(detail.get('parentStartTimeUtc'))
            if parent is None or birth is None:item['parentClaimStates']['unsupported-or-unknown-parent-tuple']+=1;continue
            item['parentClaimStates']['supported-reported-tuple']+=1;claim=parent,birth
            if claim in item['parentClaims']:item['parentClaims'][claim]['rows']+=1
            elif len(item['parentClaims'])<self.MAX_PARENT_CLAIMS:item['parentClaims'][claim]={'parentPid':parent,'parentCreation100ns':birth,'firstLine':line,'rows':1}
            else:item['unlistedParentClaimRows']+=1

    def evidence(self,availability,selection,current_pid):
        reference={'state':'unavailable','writerPid':None,'writerProcessCreation100ns':None,
            'manifestPidAgreement':None,'independentOsLifetimeVerified':False}
        value=availability.get('runningNativeBuild') if isinstance(availability,dict) else None
        if selection.get('status')!='selected':reference['state']='availability-unselected'
        elif not isinstance(value,dict):reference['state']='native-build-report-missing'
        elif type(value.get('schemaVersion')) is not int or value['schemaVersion']!=1 or value.get('evidenceScope')!='executing-main-compiled-marker-at-export;not-disk-identity-or-historical-writers':reference['state']='native-build-report-unsupported'
        else:
            pid=self._pid(value.get('writerPid'));birth=self._birth(value.get('writerProcessCreation100ns'))
            if pid is None or birth is None:reference['state']='native-writer-tuple-unknown'
            else:
                reference.update(writerPid=pid,writerProcessCreation100ns=birth)
                validManifest=isinstance(current_pid,str) and re.fullmatch(r'[1-9][0-9]{0,9}',current_pid) and int(current_pid)<(1<<32)
                reference['manifestPidAgreement']=None if not validManifest else current_pid==pid
                reference['state']='reported-native-reference' if reference['manifestPidAgreement'] is True else 'manifest-pid-unavailable-or-different'
        output=[];pidLabels=collections.defaultdict(set);bindings=collections.Counter()
        for (pid,instance),source in self.instances.items():
            item={key:value for key,value in source.items() if key not in ('parentClaims','parentClaimStates','runIdStates')}
            item['eventCounts']=dict(source['eventCounts']);item['parentClaims']=[dict(value) for value in source['parentClaims'].values()];item['parentClaimStates']=dict(source['parentClaimStates']);item['runIdStates']=dict(source['runIdStates'])
            claims=item['parentClaims'];bad=source['startRows']!=source['parentClaimStates'].get('supported-reported-tuple',0) or source['unlistedParentClaimRows']>0
            if reference['state']!='reported-native-reference':state='native-reference-unavailable'
            elif source['startRows']==0:state='no-start-parent-report-in-member'
            elif bad or len(claims)!=1:state='ambiguous-or-incomplete-start-parent-report'
            elif claims[0]['parentPid']!=reference['writerPid']:state='reported-parent-pid-differs'
            elif claims[0]['parentCreation100ns']!=reference['writerProcessCreation100ns']:state='reported-parent-pid-matches-birth-differs'
            else:state='reported-parent-tuple-matches-native-reference'
            item['parentReferenceState']=state;item['osParentageOrHostCurrentnessProven']=False;item['hostProcessBirthKnown']=False
            bindings[state]+=1;pidLabels[pid].add(instance);output.append(item)
        return {'state':'reported-bounded-logger-instances' if self.rows else 'no-object-rows','scope':'one-FanHost-carrier;reported-PID-and-HostDiagnosticLog-instance;no-cross-carrier-merge',
            'parsedObjectRowsInspected':self.rows,'unsupportedSchemaRows':self.unsupported,'invalidWriterTupleRows':self.invalid,
            'unretainedIdentityRows':self.omitted,'retainedInstanceCount':len(output),'maxInstances':self.MAX_INSTANCES,
            'maxEventKindsPerInstance':self.MAX_EVENT_KINDS,'maxParentClaimsPerInstance':self.MAX_PARENT_CLAIMS,
            'nativeReference':reference,'instances':output,'parentReferenceStateCounts':dict(bindings),
            'pidLabelsWithMultipleLoggerIds':[{'reportedPid':pid,'loggerIds':sorted(ids)} for pid,ids in sorted(pidLabels.items()) if len(ids)>1],
            'nativePidFilterIsHostCurrentness':False,'loggerGuidOrRunIdIsOsBirthProof':False,'rowsAreAllFaultsInferred':False,
            'frequencyOrLaneViolationInferred':False,'timestampsProveContinuousCoverage':False,'countsDeduplicatedAcrossFiles':False,
            'readingRules':['fault filename includes finite responsibility and state-boundary receipts;volume alone is not repeated fault or frequency proof',
                'HostDiagnosticLog GUID is a reported logger-object ID;the same PID with multiple IDs does not prove OS PID reuse or restart',
                'only host.instance-started details parent PID and exact7-fraction UTC start may compare to the selected Native report;no other owner/subject PID fallback',
                'parent equality is same-log reported reference only;no OS parentage,current Host,loaded-source or physical outcome proof',
                'first/last times follow retained file order;omitted identities/events/claims and other carriers remain separate;never invent lifetimes']}


class FanHostOutcomeEvidence:
    """Per-member counts of selected FanHost observations and exact boundary pairs."""

    REMOTE_SETTLEMENT = 'power.remote-resource-settlement'
    POWER_BOUNDARY = 'oem.power-boundary'
    POWER_BOUNDARY_SAMPLE = 'oem.power-boundary-sample'
    POWER_BOUNDARY_SUMMARY = 'oem.power-boundary-summary'
    POWER_HOOK_STATUS = 'oem.power-hook-unavailable'
    POWER_SCOPE_MARKER = 'powerObservationScopeSchema'
    POWER_SCOPE_V1 = {
        'hcSourcePolicy': 'locked-baseline-unmodified',
        'registryScenarioOperation': 'not-observed-no-new-registry-operation',
        'requestedTdp': 'not-observed', 'oemPlReadback': 'not-observed',
        'verificationScope': 'Host-invocation-boundary-only-not-leaf-WMI-or-physical-power',
    }
    MAX_BOUNDARY_SCOPES = 4096
    TERMINAL_CONTEXT = 'fan-power-boundary-terminal-v1'
    BOUNDARY_CALL_SITES = {'OpenHcDevice': 'OpenHcDevice', 'CloseHcDevice': 'CloseHcDevice'} | {
        'ApplyPowerProfile/' + source: 'ApplyPowerProfile' for source in (
            'Background', 'ProfilesPage', 'QuickProfilesPage', 'QuickProfilesCreation', 'Creation',
            'Serializer', 'LibraryUpdate', 'QuickProfilesEnable', 'LayoutPage')}
    SETTLEMENT_DELIVERIES = ('Acknowledged', 'Rejected', 'Unknown')
    SETTLEMENT_RESOURCE_FIELDS = {
        **dict.fromkeys(('references', 'workers', 'calls', 'sdk', 'leases'), int),
        'deliveryAttempted': None,
        'delivery': SETTLEMENT_DELIVERIES,
        'finalFlags': ('AllReturned', 'None'),
    }
    BOUNDARY_FIELDS = {
        'state': ('entered', 'scope-ended'),
        'outcome': ('not-observed-use-existing-Host-result-events',),
        'leafEvidence': ('not-observed',),
    }
    RELEASE_FIELDS = dict.fromkeys((
        'releaseConfirmed', 'hardwareWritesObserved', 'oemRestoreConfirmed',
        'oemPhysicalOwnershipConfirmed', 'completedHcClose', 'ownershipClaim',
    ), None)
    EVENTS = {
        POWER_BOUNDARY: ('invocation-boundary', BOUNDARY_FIELDS),
        POWER_BOUNDARY_SAMPLE: ('invocation-boundary-detail', BOUNDARY_FIELDS),
        'oem.power-boundary-summary': ('repeated-invocation-summary', {
            'evidenceScope': ('repeated-Host-boundaries-only-not-WMI-or-physical-success',),
        }),
        'oem.power-hook-unavailable': ('observation-hook-status', {
            'leafEvidence': ('not-observed-pinned-HC-has-no-observation-hook',),
        }),
        'restore.release-unproven-no-live-session': ('reported-restore-axes', RELEASE_FIELDS),
        'restore.not-applicable-no-write-history': ('reported-restore-axes', RELEASE_FIELDS),
        'restore.close-hc-already-retired': ('reported-previous-close', dict.fromkeys((
            'hcCloseComplete', 'newReleaseActionClaimed', 'physicalOwnershipClaimed',
        ), None)),
        'host.exit-forced': ('reported-exit-intent', dict.fromkeys((
            'cleanupConfirmed', 'processExitProven',
        ), None)),
        'hc-operation.write-cancelled': ('reported-write-cancellation', {
            'hardwareFailureClaimed': None, 'writesMayHaveOccurred': None,
            'cancellationCode': ('FAN_WRITE_SUPERSEDED', 'HC_OPEN_EVENTS_POWER_SUPERSEDED', 'FAN_PUBLIC_POWER_SUPERSEDED'),
            'failureOrigin': ('Unclassified', 'PublicAdmission', 'BusinessDeadline'),
        }),
        'power.resume-placeholder-lease-kept': ('reported-resume-placeholder-decision', {
            'reason': ('no-lease-present', 'lease-not-owned-by-resume-worker', 'worker-exit-without-commit'),
            'resumeTransitionReason': ('resume-commit-complete', 'public-power-superseded', 'user-control-cancelled'),
            'controlResponsibility': ('host-resume-intent',), 'nextAdjustmentStillRequired': None,
        }),
        'hc-abandoned-device-open.detected': ('reported-open-without-session', {
            'source': ('runtime-sample',),
        }),
        'power.fault-locked': ('reported-fault-state', {'state': ('FaultLocked',)}),
        'recovery.retry-terminal': ('reported-retry-terminal', {'state': ('FaultLocked',)}),
        'recovery.cycle-exhausted-without-settlement': ('reported-unsettled-cycle', {}),
        REMOTE_SETTLEMENT: ('reported-remote-resource-settlement', {
            'purpose': ('Observation', 'FanResume', 'FanWrite'),
            'outcome': ('Unknown', 'Returned', 'RetainedResources'),
            'flags': 'settlement-flags',
            'delivery': SETTLEMENT_DELIVERIES,
            'replayAllowed': None,
        }),
    }

    @staticmethod
    def _field_state(obj, field, choices):
        if field not in obj:
            return 'missing'
        value = obj[field]
        if value is None:
            return 'null'
        if choices is None:
            return ('true' if value else 'false') if type(value) is bool else 'invalid-type'
        if choices is int or choices == 'settlement-flags':
            if type(value) is not int:
                return 'invalid-type'
            upper = (1 << 31) if choices is int else (1 << 32)
            if not 0 <= value < upper:
                return 'out-of-range'
            if choices is int:
                return 'zero' if value == 0 else 'nonzero'
            if value & ~31:
                return 'unknown-bits'
            return 'all-returned' if value == 31 else 'none' if value == 0 else 'partial'
        if not isinstance(value, str):
            return 'invalid-type'
        return value if value in choices else 'unrecognized-value'

    @staticmethod
    def _object_shape(obj, field):
        value = obj.get(field)
        return ('object' if isinstance(value, dict) else 'missing' if field not in obj
                else 'null' if value is None else 'invalid-type')

    @staticmethod
    def _field_slots(fields):
        return {key: {'stateCounts': collections.Counter(), 'firstWitnessLines': {}} for key in fields}

    @classmethod
    def _boundary_context_state(cls, row, details):
        shape = cls._object_shape(row, 'context')
        if shape != 'object':
            return shape
        context = row['context']
        if 'contextScope' in context:
            if context['contextScope'] != cls.TERMINAL_CONTEXT:
                return 'unrecognized-context-scope'
            if not isinstance(details, dict) or details.get('state') != 'scope-ended':
                return 'scope-phase-conflict'
            return 'reported-compact-terminal'
        return 'provider-error' if isinstance(context.get('contextError'), str) else 'unmarked-object'

    @staticmethod
    def _boundary_pair_key(row, details):
        pid = FanHostDiagnosticInstanceEvidence._pid(row.get('processId'))
        instance = row.get('hostInstanceId')
        if pid is None or not isinstance(instance, str) or re.fullmatch(r'[0-9a-f]{32}', instance) is None:
            return None
        metadata = details.get('metadata') if isinstance(details, dict) else None
        if not isinstance(metadata, dict):
            return None
        scope = metadata.get('fanScope')
        if not isinstance(scope, str) or scope != metadata.get('operationId'):
            return None
        match = re.fullmatch(r'fan:([1-9][0-9]{0,9}):([1-9][0-9]{0,18})', scope)
        if match is None or match[1] != pid or int(match[2]) >= (1 << 63):
            return None
        return pid, instance, scope

    def _observe_boundary_pair(self, event, line, row, details):
        view = self.boundary_pairs.setdefault(event, {'groups': {}, 'unkeyedRows': 0, 'limitExceeded': False})
        key = self._boundary_pair_key(row, details)
        if key is None:
            view['unkeyedRows'] += 1
            return
        if view['limitExceeded']:
            return
        groups = view['groups']
        if key not in groups:
            if len(groups) >= self.MAX_BOUNDARY_SCOPES:
                view['limitExceeded'] = True
                groups.clear()
                return
            groups[key] = []
        witnesses = groups[key]
        if len(witnesses) < 3:
            phase = details.get('state')
            phase = phase if phase in ('entered', 'scope-ended') else None
            terminal = self._boundary_context_state(row, details) if phase == 'scope-ended' else None
            site = details['metadata'].get('callSite')
            site = site if isinstance(site, str) and site in self.BOUNDARY_CALL_SITES else None
            tick = self._boundary_timer_field(details, 'monotonicTick')
            duration = self._boundary_timer_field(details, 'durationMs') if phase == 'scope-ended' else None
            witnesses.append((line, phase, terminal, site, tick, duration))

    @staticmethod
    def _boundary_timer_field(details, field):
        if field not in details:
            return 'missing'
        value = details[field]
        if value is None:
            return 'null'
        if type(value) is not int:
            return 'invalid-type'
        return value if 0 <= value < (1 << 63) else 'out-of-range'

    def _observe_boundary_timing(self, witnesses, timing):
        (start_line, _, _, start_site, start_tick, _), (end_line, _, _, end_site, end_tick, duration) = witnesses
        fields = (('entered-tick', start_tick), ('terminal-tick', end_tick), ('terminal-duration', duration))
        lines = [start_line, end_line]
        witness = {'lines': lines}
        missing = next((f'{name}-{value}' for name, value in fields if isinstance(value, str)), None)
        if missing:
            state = missing
        else:
            delta = end_tick - start_tick
            witness.update(tickDeltaMs=delta, reportedDurationMs=duration)
            if delta < 0:
                state = 'ticks-reversed'
            elif duration != delta:
                # The writer reads the endpoint tick and the duration separately.
                state = 'duration-exceeds-tick-delta' if duration > delta else 'duration-below-tick-delta'
            else:
                state = 'duration-matches-ticks'
                site = self.BOUNDARY_CALL_SITES.get(start_site, 'unattributed') if start_site == end_site else 'unattributed'
                bucket = timing['matchingByCallSite'].setdefault(site, {'pairs': 0, 'maxMs': None, 'maxLines': None})
                bucket['pairs'] += 1
                if bucket['maxMs'] is None or duration > bucket['maxMs']:
                    bucket.update(maxMs=duration, maxLines=lines)
        timing['stateCounts'][state] += 1
        timing['firstWitnesses'].setdefault(state, witness)

    def _boundary_pair_evidence(self):
        result = {}
        for event, view in self.boundary_pairs.items():
            capped = view['limitExceeded']
            states, terminals, first = collections.Counter(), collections.Counter(), {}
            timing = {'stateCounts': collections.Counter(), 'firstWitnesses': {}, 'matchingByCallSite': {}}
            for witnesses in view['groups'].values():
                phases = tuple(w[1] for w in witnesses)
                if phases == ('entered', 'scope-ended'):
                    state = 'paired-in-order'
                    terminals[witnesses[1][2]] += 1
                    self._observe_boundary_timing(witnesses, timing)
                elif phases == ('scope-ended', 'entered'):
                    state = 'reversed'
                elif phases == ('entered',):
                    state = 'entered-only'
                elif phases == ('scope-ended',):
                    state = 'terminal-only'
                else:
                    state = 'ambiguous'
                states[state] += 1
                first.setdefault(state, [w[0] for w in witnesses])
            result[event] = {'state': 'scope-limit-exceeded' if capped else 'observed-within-limit',
                             'maxScopes': self.MAX_BOUNDARY_SCOPES, 'unkeyedRows': view['unkeyedRows'],
                             'scopeStateCounts': None if capped else dict(states),
                             'firstWitnessLines': None if capped else first,
                             'pairedTerminalContextCounts': None if capped else dict(terminals)}
            if capped or timing['stateCounts']:
                result[event]['pairedTiming'] = None if capped else timing
        return result

    def _observe_fields(self, slots, fields, obj, shape, carrier, line):
        for key, choices in fields.items():
            state = self._field_state(obj, key, choices) if shape == 'object' else carrier + '-' + shape
            slot = slots[key]
            slot['stateCounts'][state] += 1
            slot['firstWitnessLines'].setdefault(state, line)

    WRITE_FAILURE_FIELD = 'logWriteFailureEvidence'
    WRITE_STAGES = {'faultLedger': ('append',), 'runtime': ('active-length', 'rotation', 'append')}

    @classmethod
    def _power_scope_name(cls, event, details):
        if not isinstance(details, dict):
            return None
        if event == cls.POWER_HOOK_STATUS:
            metadata = details
        elif event in (cls.POWER_BOUNDARY, cls.POWER_BOUNDARY_SAMPLE, cls.POWER_BOUNDARY_SUMMARY):
            metadata = details.get('metadata')
        else:
            return None
        if not isinstance(metadata, dict) or cls.POWER_SCOPE_MARKER not in metadata:
            return None
        version = metadata[cls.POWER_SCOPE_MARKER]
        if type(version) is not int or version != 1:
            return 'unsupported-schema'
        if any(key in metadata and metadata[key] != expected for key, expected in cls.POWER_SCOPE_V1.items()):
            return 'conflicting-fixed-field'
        return 'reported-fixed-scope-v1'

    def _observe_write_failure(self, line, row):
        if self.WRITE_FAILURE_FIELD not in row:
            return
        value = row[self.WRITE_FAILURE_FIELD]
        view = self.write_failures
        view['receiptRows'] += 1
        if not isinstance(value, dict):
            view['receiptStates']['null' if value is None else 'invalid-type'] += 1
            return
        if type(value.get('version')) is not int or value['version'] != 1:
            view['receiptStates']['unsupported-version'] += 1
            return
        pid, instance = row.get('processId'), row.get('hostInstanceId')
        if (type(pid) is not int or not 0 < pid < 1 << 32 or not isinstance(instance, str) or
                re.fullmatch(r'[0-9a-f]{32}', instance) is None or row.get('runId') != f'fan-host:{pid}:{instance}'):
            view['receiptStates']['invalid-writer'] += 1
            return
        if not {'version'} < value.keys() <= {'version', *self.WRITE_STAGES}:
            view['receiptStates']['invalid-fields'] += 1
            return
        view['receiptStates']['reported'] += 1
        for sink, stages in self.WRITE_STAGES.items():
            if sink not in value:
                continue
            item = value[sink]
            stats = view['sinks'].setdefault(sink, {'states': collections.Counter(), 'firstReported': None, 'lastReported': None})
            if not isinstance(item, dict):
                stats['states']['null' if item is None else 'invalid-type'] += 1
                continue
            count, hr, stage = item.get('count'), item.get('lastHResult'), item.get('lastStage')
            if (item.keys() != {'count','lastHResult','lastStage'} or
                    type(count) is not int or not 0 < count < (1 << 63) or
                    type(hr) is not int or not -(1 << 31) <= hr < (1 << 31) or stage not in stages):
                stats['states']['invalid-fields'] += 1
                continue
            stats['states']['reported'] += 1
            witness = dict(line=line, processId=pid, hostInstanceId=instance, count=count, lastHResult=hr,
                           lastStage=stage, countAtLimit=count == (1 << 63) - 1)
            if stats['firstReported'] is None:
                stats['firstReported'] = witness
            stats['lastReported'] = witness

    def __init__(self, records):
        self.rows = self.unsupported = self.other = 0
        self.write_failures = dict(receiptRows=0, receiptStates=collections.Counter(), sinks={},
            scope='prior-observed-file-call-failures;logger-instance-cumulative;first-and-last-retained-receipts',
            countsAdditive=False, currentRowWriteOrDurabilityProven=False)
        self.root_error = collections.Counter()
        self.events = {}
        self.boundary_pairs = {}
        for line, row in records:
            self.rows += 1
            schema = row.get('schemaVersion')
            diagnostic = row.get('diagnosticSchemaVersion')
            if type(schema) is not int or schema != 2 or type(diagnostic) is not int or diagnostic != 1:
                self.unsupported += 1
                continue
            self._observe_write_failure(line, row)
            error = row.get('error')
            if isinstance(error, str):
                root_state = 'not-observed' if error == 'not-observed' else 'reported-text' if error else 'empty-text'
            else:
                root_state = self._field_state(row, 'error', ())
            self.root_error[root_state] += 1
            event = row.get('eventName')
            if not isinstance(event, str) or event not in self.EVENTS:
                self.other += 1
                continue
            kind, fields = self.EVENTS[event]
            if event not in self.events:
                self.events[event] = {'kind': kind, 'rows': 0, 'firstLine': line, 'lastLine': line,
                    'detailsShapeCounts': collections.Counter(), 'fields': self._field_slots(fields)}
                if event == self.REMOTE_SETTLEMENT:
                    self.events[event]['resourceEvidence'] = {
                        'shapeCounts': collections.Counter(), 'fields': self._field_slots(self.SETTLEMENT_RESOURCE_FIELDS)}
            item = self.events[event]
            item['rows'] += 1
            item['lastLine'] = line
            details = row.get('details')
            shape = self._object_shape(row, 'details')
            item['detailsShapeCounts'][shape] += 1
            self._observe_fields(item['fields'], fields, details, shape, 'details', line)
            scope_state = self._power_scope_name(event, details)
            if scope_state is not None:
                scope = item.setdefault('powerObservationScope', {'stateCounts': collections.Counter(), 'firstWitnessLines': {}})
                scope['stateCounts'][scope_state] += 1
                scope['firstWitnessLines'].setdefault(scope_state, line)
            if event in (self.POWER_BOUNDARY, self.POWER_BOUNDARY_SAMPLE):
                self._observe_boundary_pair(event, line, row, details)
                phase = details.get('state') if isinstance(details, dict) else None
                raw_context = row.get('context')
                if (event == self.POWER_BOUNDARY or phase in ('entered', 'scope-ended') or
                        (isinstance(raw_context, dict) and 'contextScope' in raw_context)):
                    context = item.setdefault('contextCoverage', {'stateCounts': collections.Counter(), 'firstWitnessLines': {}})
                    context_state = self._boundary_context_state(row, details)
                    context['stateCounts'][context_state] += 1
                    context['firstWitnessLines'].setdefault(context_state, line)
            if event == self.REMOTE_SETTLEMENT:
                nested = item['resourceEvidence']
                resources = details.get('resourceEvidence') if shape == 'object' else None
                resource_shape = self._object_shape(details, 'resourceEvidence') if shape == 'object' else 'details-' + shape
                nested['shapeCounts'][resource_shape] += 1
                self._observe_fields(nested['fields'], self.SETTLEMENT_RESOURCE_FIELDS,
                                     resources, resource_shape, 'resourceEvidence', line)

    def evidence(self):
        return {
            'schemaVersion': 1,
            'scope': 'one-retained-FanHost-carrier;schema2/diagnostic1;fixed-row-marginals',
            'parsedObjectRowsInspected': self.rows,
            'unsupportedSchemaRows': self.unsupported,
            'unclassifiedOutcomeRows': self.other,
            'selectedOutcomeRows': self.rows - self.unsupported - self.other,
            'rootErrorFieldStates': dict(self.root_error),
            **({self.WRITE_FAILURE_FIELD: self.write_failures} if self.write_failures['receiptRows'] else {}),
            'events': self.events,
            'boundaryPairing': self._boundary_pair_evidence(),
            'readingRules': [
                'root error not-observed means this field lacks an error observation; it is not proof of health',
                'scope-ended and boundary summaries report invocation boundaries, not leaf results',
                'a marked compact terminal is a same-row context projection; omitted values are not copied from the entered row',
                'boundary pairs stay within one member and event; a missing side does not prove an unfinished call',
                'reported restore, close, cancellation or exit intent does not certify physical ownership, hardware success or process exit',
                'the lease decision and resume transition reason are separate; open without a registered session does not identify its origin or prove a hardware fault',
                'remote settlement outcome and flags, delivery, and local resource ledger are separate reports; none proves physical or OS-wide return',
                'unknown, null, missing and false are distinct; only named fields from the same row are read',
                'counts are retained rows, not unique faults, operations, a time rate or a complete history',
            ],
        }


RESIDENT_VOLUME_LANES = frozenset(value for value in LANES.values() if value.endswith('-low')) | {
    'virtual-adaptation-bounded', 'fan-host-fault'}


def resident_event_byte_evidence(data, groups):
    """Measure selected parsed object rows in their original LF-delimited member."""
    from heapq import merge

    def line_keys(name, records):
        for line, _ in records:
            yield line, name

    selected = {name: dict(state='observed', rowsMeasured=0, physicalLineBytes=0,
                           largestLineBytes=0, largestLine=None) for name in groups}
    previous_line = cursor = 0
    for line, name in merge(*(line_keys(name, rows) for name, rows in groups.items())):
        # Sorted parser line numbers let us scan the raw buffer without copying slices or parsing again.
        for _ in range(line - previous_line):
            start = cursor
            lf = data.find(b'\n', start)
            cursor = len(data) if lf < 0 else lf + 1
        previous_line = line
        size = cursor - start
        item = selected[name]
        item['rowsMeasured'] += 1
        item['physicalLineBytes'] += size
        if size > item['largestLineBytes']:
            item['largestLineBytes'] = size
            item['largestLine'] = line
    return selected


def resident_reported_ok_field_evidence(records, current_pid, filename):
    """Read only the root ok field; no event/status/nested-field inference."""
    categories = ('true', 'false', 'missing', 'null', 'nonBoolean')
    counts = {key: dict.fromkeys(('all', 'currentPidMatch', 'otherPid', 'pidUnknown'), 0) for key in categories}
    first_false = last_false = None
    for line, row in records:
        value = row.get('ok')
        key = ('missing' if 'ok' not in row else 'null' if value is None else
               'true' if value is True else 'false' if value is False else 'nonBoolean')
        pid = process_identity(row, filename)[0]
        scope = 'pidUnknown' if pid == 'unknown' else 'currentPidMatch' if current_pid and pid == current_pid else 'otherPid'
        counts[key]['all'] += 1
        counts[key][scope] += 1
        if key == 'false':
            if first_false is None:
                first_false = line
            last_false = line
    return {'state': 'observed', 'rowsInspected': len(records), 'counts': counts,
            'firstFalseLine': first_false, 'lastFalseLine': last_false}


def gamepad_ui_gate_evidence_record(availability):
    """Typed current export observation;never reconstruct absent runtime counters."""
    result={'state':'missing','writerPid':None,'writerProcessCreation100ns':None,
            'detailedEnabledAtExport':None,'counters':None,'fieldIssues':[],
            'countsArePresses':False,'zeroIsNoDeviceProof':False,'writerLifetimeVerified':False,
            'snapshotAtomicAcrossCounters':False,'upstreamCallsCovered':False,'actionOrDeliveryProven':False}
    if not isinstance(availability,dict):result['state']='invalid-availability';return result
    if 'gamepadUiGateEvidence' not in availability:return result
    value=availability['gamepadUiGateEvidence']
    if value is None:result['state']='null';return result
    if not isinstance(value,dict):result['state']='invalid-type';return result
    if type(value.get('schema')) is not int or value['schema']!=1:result['state']='unsupported-schema';return result
    issues=result['fieldIssues'];writer=value.get('writerPid');birth=value.get('writerProcessCreation100ns')
    if type(writer) is int and 0<writer<(1<<32):result['writerPid']=str(writer)
    else:issues.append('writerPid:missing-or-invalid')
    if 'writerProcessCreation100ns' not in value:issues.append('writerProcessCreation100ns:missing')
    elif birth is None:pass
    elif isinstance(birth,str) and 0<len(birth)<=20 and not birth.startswith('0') and birth.isascii() and birth.isdecimal() and int(birth)<(1<<64):result['writerProcessCreation100ns']=birth
    else:issues.append('writerProcessCreation100ns:invalid-decimal-string')
    enabled=value.get('detailedEnabledAtExport')
    if type(enabled) is bool:result['detailedEnabledAtExport']=enabled
    else:issues.append('detailedEnabledAtExport:missing-or-invalid')
    counters=value.get('counters');typed={}
    for gate in ('radial-owner','ui-ineligible','ui-eligible'):
        row=counters.get(gate) if isinstance(counters,dict) else None;typed[gate]={}
        for key in ('calls','nonzeroButtons','changedButtons'):
            item=row.get(key) if isinstance(row,dict) else None
            if type(item) is int and 0<=item<(1<<64):typed[gate][key]=item
            else:typed[gate][key]=None;issues.append('counters.'+gate+'.'+key+':missing-or-invalid')
    result['counters']=typed;result['state']='invalid-fields' if issues else 'reported-counters'
    result['scope']='same-export-reported-process-lifetime-detail-enabled-gate-counts;not-presses-or-complete-input-history'
    return result


def domain_writer_envelope_record(row):
    """Same-row reported Domain writer metadata; never borrow a manifest birth."""
    result={'state':'legacy-writer-unavailable','writerPid':None,'writerProcessCreation100ns':None,
            'reportedWriterTupleComplete':False,'writerLifetimeVerified':False,'fieldIssues':[],
            'payloadSubjectPidUsedAsWriter':False}
    if not isinstance(row,dict):result['state']='invalid-fields';result['fieldIssues']=['row:invalid-type'];return result
    def process(v):return str(v) if type(v) is int and 0<v<(1<<32) else None
    writer=process(row.get('writerPid'));result['writerPid']=writer
    if 'writerEnvelopeSchema' not in row:
        result['state']='legacy-pid-only' if writer is not None else 'legacy-writer-unavailable'
        result['unversionedBirthPresent']='writerProcessCreation100ns' in row
        raw_birth=row.get('writerProcessCreation100ns')
        birth_state='absent' if not result['unversionedBirthPresent'] else 'reported-null' if raw_birth is None else 'invalid-value'
        valid_raw_birth=(isinstance(raw_birth,str) and 0<len(raw_birth)<=20 and not raw_birth.startswith('0') and raw_birth.isascii() and raw_birth.isdecimal() and int(raw_birth)<(1<<64))
        if valid_raw_birth:birth_state='reported-decimal-string'
        result['unversionedBirthObservation']={'state':birth_state,'value':raw_birth if valid_raw_birth else None,
            'scope':'same-row-unversioned-raw-field;origin-not-verified','writerIdentityProven':False}
        return result
    schema=row['writerEnvelopeSchema']
    if type(schema) is not int or schema!=1:result['state']='unsupported-envelope';return result
    issues=result['fieldIssues']
    if writer is None:issues.append('writerPid:missing-or-invalid')
    if 'writerProcessCreation100ns' not in row:issues.append('writerProcessCreation100ns:missing')
    else:
        birth=row['writerProcessCreation100ns']
        if birth is None:pass
        elif isinstance(birth,str) and birth.isascii() and birth.isdecimal() and 0<len(birth)<=20 and not birth.startswith('0') and int(birth)<(1<<64):result['writerProcessCreation100ns']=birth
        else:issues.append('writerProcessCreation100ns:invalid-decimal-string')
    if issues:result['state']='invalid-fields';return result
    result['reportedWriterTupleComplete']=result['writerProcessCreation100ns'] is not None
    result['state']='reported-pid-and-birth' if result['reportedWriterTupleComplete'] else 'reported-pid-birth-unavailable'
    return result


def gyro_sample_tick(row):
    tick = row.get('sampleTickMs')
    return tick if type(tick) is int and 0 <= tick < 1 << 64 else None


def gyro_sample_cadence_evidence(samples):
    """Count retained clock gaps independently of known sensor receipt ages."""
    if not any('sampleTickMs' in row for _, row in samples):
        return None  # Legacy rows cannot supply a monotonic capture clock.
    groups, states, previous = {}, collections.Counter(), None
    max_groups = 16
    for line, row in samples:
        prior, previous = previous, None
        tick = gyro_sample_tick(row)
        if tick is None:
            states['invalid-tick' if 'sampleTickMs' in row else 'missing-tick'] += 1
            continue
        writer = domain_writer_envelope_record(row)
        family = row.get('sourceFamily')
        if (not writer['reportedWriterTupleComplete'] or not isinstance(family, str) or
                re.fullmatch(r'[a-zA-Z0-9_-]{1,32}', family) is None):
            states['identity-unavailable'] += 1
            continue
        key = writer['writerPid'], writer['writerProcessCreation100ns'], family
        group = groups.get(key)
        if group is None:
            if len(groups) >= max_groups:
                states['group-limit'] += 1
                continue
            group = groups[key] = dict(writerPid=int(key[0]), writerProcessCreation100ns=key[1], sourceFamily=family,
                                        ticksObserved=0, pairCounts=collections.Counter(), largestGap=None)
        group['ticksObserved'] += 1
        states['reported-tick'] += 1
        if prior is not None and prior[0] == key and prior[1] + 1 == line:
            gap = tick - prior[2]
            group['pairCounts']['tick-regressed' if gap < 0 else 'under-1000-ms' if gap < 1000 else
                                '1000-to-1999-ms' if gap < 2000 else 'at-least-2000-ms'] += 1
            largest = group['largestGap']
            if gap >= 0 and (largest is None or gap > largest['elapsedMs']):
                group['largestGap'] = dict(beforeLine=prior[1], afterLine=line, elapsedMs=gap)
        previous = key, line, tick
    return dict(schema=1, scope='physical-neighbors-in-one-member;same-reported-writer-and-source;not-missing-sample-count',
                nominalMinimumMs=1000, maxGroups=max_groups, tickStates=dict(states), writerGroups=list(groups.values()))


def gyro_receipt_age_evidence(samples):
    """Summarize already parsed periodic rows from one member, in file order."""
    gyro_min_ms, accel_max_ms, gyro_fresh_ms, max_groups = 5000, 100, 100, 16
    groups = {}
    skipped = collections.Counter()
    previous = None

    def counter(value):
        return value if type(value) is int and 0 <= value < (1 << 64) else None

    def windows_view(row):
        view = row.get('windowsView')
        if (not isinstance(view, dict) or counter(view.get('schema')) != 1 or
                view.get('route') not in ('default', 'private')):
            return None
        binding, choice, gyro, accel = (counter(view.get(key)) for key in
            ('bindingGeneration', 'choiceGeneration', 'gyroRejected', 'accelRejected'))
        if not binding or not choice or gyro is None or accel is None:
            return None
        return (view['route'], binding, choice), gyro, accel

    for line, row in samples:
        prior, previous = previous, None
        gyro_age = counter(row.get('gyroReceiptAgeMs'))
        accel_age = counter(row.get('accelReceiptAgeMs'))
        if gyro_age is None or accel_age is None:
            skipped['age-unavailable'] += 1
            continue
        writer = domain_writer_envelope_record(row)
        if not writer['reportedWriterTupleComplete']:
            skipped['writer-tuple-unavailable'] += 1
            continue
        family = row.get('sourceFamily')
        if not isinstance(family, str) or re.fullmatch(r'[a-zA-Z0-9_-]{1,32}', family) is None:
            skipped['source-family-unavailable'] += 1
            continue
        key = writer['writerPid'], writer['writerProcessCreation100ns'], family
        group = groups.get(key)
        if group is None:
            if len(groups) >= max_groups:
                skipped['group-limit'] += 1
                continue
            group = dict(writerPid=int(key[0]), writerProcessCreation100ns=key[1], sourceFamily=family,
                         rowsWithBothAges=0, contrastingRows=0, withProcessorReady=0,
                         withMotionEnabledAndHostAlive=0, first=None, last=None, maxGyroAge=None,
                         sequenceContrast=dict(adjacentPairs=0, longestRun=None, windowsPairsByState={}),
                         gyroSequenceChangeAfterContrast={})
            groups[key] = group
        group['rowsWithBothAges'] += 1
        contrasting = gyro_age >= gyro_min_ms and accel_age <= accel_max_ms
        # A change must be visible on the next physical line. A rejected row,
        # rotation, or other interposed record cannot supply that boundary.
        after_run = (prior is not None and prior[0] == key and prior[6] >= 2 and
                     line == prior[1]['line'] + 1)
        if not contrasting and not after_run:
            continue
        ref = dict(line=line, gyroReceiptAgeMs=gyro_age, accelReceiptAgeMs=accel_age,
                   gyroSeq=counter(row.get('gyroSeq')), accelSeq=counter(row.get('accelSeq')))
        sample_tick = gyro_sample_tick(row)
        if sample_tick is not None:
            ref['sampleTickMs'] = sample_tick
        for field in ('processorReady', 'motionEnabled', 'hostAlive'):
            value = row.get(field)
            ref[field] = value if type(value) is bool else None
        view = windows_view(row) if ref['gyroSeq'] and ref['accelSeq'] else None
        if after_run and ref['gyroSeq'] and ref['accelSeq'] and ref['gyroSeq'] != prior[1]['gyroSeq']:
            _, earlier, earlier_view, _, _, run_start, run_rows = prior
            both_views_reported = view is not None and earlier_view is not None
            if ref['gyroSeq'] < earlier['gyroSeq'] or ref['accelSeq'] < earlier['accelSeq']:
                state = 'sequence-regressed'
            elif both_views_reported and view[0] != earlier_view[0]:
                state = 'reported-view-changed'
            elif both_views_reported and (view[1] < earlier_view[1] or view[2] < earlier_view[2]):
                state = 'reported-rejection-counter-regressed'
            elif gyro_age <= gyro_fresh_ms and accel_age <= accel_max_ms and ref['accelSeq'] > earlier['accelSeq']:
                state = 'gyro-advanced-both-fresh'
            else:
                state = 'gyro-advanced-other'
            counts = group['gyroSequenceChangeAfterContrast']
            if state not in counts:
                counts[state] = dict(count=0, withSameReportedView=0, first=None, last=None)
            summary = counts[state]
            same_view = bool(both_views_reported and view[0] == earlier_view[0])
            change = dict(contrastSampleRows=run_rows, contrastFirstLine=run_start,
                          before=earlier, after=ref, sameReportedView=same_view if both_views_reported else None)
            before_tick, after_tick = earlier.get('sampleTickMs'), sample_tick
            if before_tick is not None and after_tick is not None:
                change['sampleTickComparison'] = (dict(state='tick-regressed') if after_tick < before_tick else
                                                  dict(state='reported-gap', elapsedMs=after_tick-before_tick))
            summary['count'] += 1
            summary['withSameReportedView'] += same_view
            if summary['first'] is None:
                summary['first'] = change
            summary['last'] = change
        if not contrasting:
            continue
        group['contrastingRows'] += 1
        group['withProcessorReady'] += row.get('processorReady') is True
        group['withMotionEnabledAndHostAlive'] += row.get('motionEnabled') is True and row.get('hostAlive') is True
        if group['first'] is None:
            group['first'] = ref
        group['last'] = ref
        if group['maxGyroAge'] is None or gyro_age > group['maxGyroAge']['gyroReceiptAgeMs']:
            group['maxGyroAge'] = ref
        if not ref['gyroSeq'] or not ref['accelSeq']:
            continue
        start, run, physical_start, physical_run = ref, 1, line, 1
        if prior is not None:
            prior_key, earlier, earlier_view, earlier_start, earlier_run, earlier_physical_start, earlier_physical_run = prior
            if (key == prior_key and ref['gyroSeq'] == earlier['gyroSeq'] and ref['accelSeq'] > earlier['accelSeq']
                    and gyro_age > earlier['gyroReceiptAgeMs']):
                contrast = group['sequenceContrast']
                contrast['adjacentPairs'] += 1
                if view is None or earlier_view is None:
                    state = 'view-unavailable'
                elif view[0] != earlier_view[0]:
                    state = 'reported-view-changed'
                elif view[1] > earlier_view[1]:
                    state = 'gyro-rejections-increased'
                elif view[1] < earlier_view[1]:
                    state = 'gyro-rejections-decreased'
                else:
                    state = 'gyro-rejections-unchanged'
                states = contrast['windowsPairsByState']
                states[state] = states.get(state, 0) + 1
                if state not in ('reported-view-changed', 'gyro-rejections-decreased'):
                    start, run = earlier_start, earlier_run + 1
                    if line == earlier['line'] + 1 and (view is None or earlier_view is None or view[2] >= earlier_view[2]):
                        physical_start, physical_run = earlier_physical_start, earlier_physical_run + 1
                    longest = contrast['longestRun']
                    if longest is None or run > longest['sampleRows']:
                        contrast['longestRun'] = dict(sampleRows=run, first=start, last=ref)
        previous = key, ref, view, start, run, physical_start, physical_run
    return dict(schema=1, scope='retained-periodic-samples-in-this-member;not-continuity-or-device-proof',
                gyroAgeAtLeastMs=gyro_min_ms, accelAgeAtMostMs=accel_max_ms, maxGroups=max_groups,
                sequenceContrastScope='consecutive-retained-status-rows;not-continuous-time-or-provider-cause-proof',
                gyroSequenceChangeScope='next-physical-line-after-at-least-two-physically-consecutive-contrasting-samples;same-reported-writer-and-family;not-recovery-or-device-proof',
                gyroFreshAgeAtMostMs=gyro_fresh_ms,
                observedRows=len(samples), unclassifiedRows=dict(skipped), writerGroups=list(groups.values()))


class InputSourceReadbackEvidence:
    """Link retained diagnostic rows by their explicitly reported writer/window only."""
    MAX_WINDOWS = 2048
    BOUNDARY_REASONS = ('first', 'power-generation-change', 'fse-state-change', 'foreground-change')

    def __init__(self):
        self.counts = collections.Counter()
        self.windows = {}
        self.first = {}
        self.limited = False

    @staticmethod
    def _unsigned(value, bits, positive=False):
        return type(value) is int and int(positive) <= value < (1 << bits)

    def observe(self, filename, rows, metadata):
        if metadata.get('containerOnly') or not re.fullmatch(
                r'(native-detail|virtual-gamepad)\.log(?:\.(?:prev|previous|[0-9]+))?', filename):
            return
        context = filename.startswith('native-detail.')
        event = 'input-source-readback-context' if context else 'input-source-readback-pair'
        for line, row in rows:
            if row.get('event') != event:
                continue
            self.counts['contextRows' if context else 'pairRows'] += 1
            ref = {'file': filename, 'occurrence': metadata.get('archiveEntryOccurrence'), 'line': line}
            schema, version = row.get('readbackEvidenceSchema'), row.get('formatVersion')
            if 'readbackEvidenceSchema' not in row:
                state = 'legacyRows'
            elif type(schema) is not int or schema != 1 or type(version) is not int or version != 1:
                state = 'unsupportedRows'
            else:
                state = self._observe_typed(row, context, ref)
            self.counts[state] += 1
            self.first.setdefault(state, ref)

    def _observe_typed(self, row, context, ref):
        if context:
            pid = row.get('pid')
            birth = row.get('writerProcessCreation100ns')
        else:
            writer = domain_writer_envelope_record(row)
            pid = row.get('writerPid') if writer['reportedWriterTupleComplete'] else None
            birth = writer['writerProcessCreation100ns']
        ordinal = row.get('readbackWindowOrdinal')
        if not (self._unsigned(pid, 32, True) and isinstance(birth, str) and
                0 < len(birth) <= 20 and birth.isascii() and birth.isdecimal() and
                not birth.startswith('0') and int(birth) < (1 << 64) and self._unsigned(ordinal, 64, True)):
            return 'windowIdentityUnavailableRows'
        reason = row.get('reason')
        boundary = reason in self.BOUNDARY_REASONS
        if not boundary and (context or reason != 'armed-sample'):
            return 'unsupportedReasonRows'
        key = pid, birth, ordinal
        if key not in self.windows:
            if len(self.windows) >= self.MAX_WINDOWS:
                self.limited = True
                return 'windowBudgetExcludedRows'
            self.windows[key] = [0, 0, None]
        window = self.windows[key]
        if context:
            window[0] += 1
        else:
            window[1] += 1
            if window[2] is None:
                window[2] = ref
        return 'boundContextRows' if context else 'boundPairRows'

    def evidence(self):
        pairing, first = collections.Counter(), {}
        if not self.limited:
            for contexts, pairs, ref in self.windows.values():
                if not pairs:
                    continue
                state = 'uniqueContext' if contexts == 1 else 'ambiguousContext' if contexts else 'missingContext'
                pairing[state] += pairs
                first.setdefault(state, ref)
        return {'scope': 'same-archive-reported-writer-and-window;counts-are-retained-rows',
                'counts': dict(self.counts), 'firstRows': self.first,
                'pairRowsByContext': None if self.limited else dict(pairing), 'firstPairRows': first,
                'windowLimit': self.MAX_WINDOWS, 'windowLimitReached': self.limited,
                'deviceIdentityDeliveryOrInputAbsenceProven': False}


class DomainWriterEnvelopeEvidence:
    """Observe fixed Domain carriers; bounded typed head/tail/state witnesses."""
    _states=('legacy-writer-unavailable','legacy-pid-only','unsupported-envelope','invalid-fields','reported-pid-and-birth','reported-pid-birth-unavailable')
    CARRIER_RE=r'(?:gyro-motion(?:-samples)?|virtual-gamepad|focus-trace)\.log(?:\.(?:prev|previous|\d+))?'
    SCOPE='retained-three-Domain-writer-envelope-observations;not-OS-lifetime-or-device-proof'
    def row_record(self,row,filename):
        return domain_writer_envelope_record(row)
    def __init__(self,current_pid=None):
        self.current_pid=current_pid;self.counts=collections.Counter();self.carriers=collections.Counter()
        self.relations=collections.Counter();self.total=0;self.head=[];self.tail=collections.deque(maxlen=8);self.witnesses={};self.unversionedBirthCounts=collections.Counter();self.unversionedBirthWitnesses={}
    def observe(self,filename,rows,metadata):
        if not isinstance(filename,str) or re.fullmatch(self.CARRIER_RE,filename) is None:return
        base=canonical_log_name(filename)
        for line,row in rows:
            self.total+=1;value=self.row_record(row,filename);state=value['state'];self.counts[state]+=1;self.carriers[base]+=1
            writer=value['writerPid'];relation='pid-unknown' if writer is None else 'current-pid-unspecified' if self.current_pid is None else 'current-pid-match' if writer==str(self.current_pid) else 'other-pid';self.relations[relation]+=1
            ref={'encounterOrdinal':self.total,'file':filename[:160],'line':line if type(line) is int and 0<line<(1<<64) else None,
                'pidRelation':relation,'record':value}
            if len(filename)>160:ref['fileNameSha256Utf8Surrogatepass']=hashlib.sha256(filename.encode('utf-8',errors='surrogatepass')).hexdigest()
            for key in ('archiveEntryOccurrence','containerOnly'):
                item=metadata.get(key)
                if (type(item) is int and item>=0 if key=='archiveEntryOccurrence' else type(item) is bool):ref[key]=item
            if len(self.head)<8:self.head.append(ref)
            else:self.tail.append(ref)
            self.witnesses.setdefault((state,relation),ref)
            unversioned=value.get('unversionedBirthObservation')
            if unversioned is not None:
                raw_state=unversioned['state'];self.unversionedBirthCounts[raw_state]+=1;self.unversionedBirthWitnesses.setdefault(raw_state,ref)
    def evidence(self):
        selected={q['encounterOrdinal']:q for q in self.head+list(self.tail)+list(self.witnesses.values())+list(self.unversionedBirthWitnesses.values())}
        scope = ('retained-Domain-writer-envelope-observations;not-OS-lifetime-or-device-proof'
                 if self.carriers['gyro-motion-samples.log'] else self.SCOPE)
        return {'schema':1,'scope':scope,
            'rowsObserved':self.total,'countsByState':dict(self.counts),'rowsByCarrier':dict(self.carriers),'countsByPidRelation':dict(self.relations),'countsByUnversionedBirthState':dict(self.unversionedBirthCounts),
            'observations':[selected[k] for k in sorted(selected)],'maxListedObservations':44,'rowsNotListed':self.total-len(selected),
            'selection':'first8,last8,first6-fixed-envelope-states-by4-fixed-PID-relations,first4-fixed-unversioned-birth-states','rowsJoined':False,'rawRowsRemoved':False,
            'manifestBirthInferred':False,'samePidMeansSameLifetime':False,'currentPidMatchIsLifetimeProof':False,
            'oldUnversionedBirthPromotedToWriter':False,'rowsAreUniqueOperations':False}

def fan_native_writer_envelope_record(row,filename):
    """Fan Native root writer metadata;legacy subject fields never supply birth."""
    selected=isinstance(filename,str) and re.fullmatch(r'(?:fan-api|hardware-coordinator)\.log(?:\.(?:prev|previous|\d+))?',filename)
    if not selected:
        value=domain_writer_envelope_record({});value['state']='unsupported-carrier';return value
    if not isinstance(row,dict):return domain_writer_envelope_record(row)
    mapped={key:row[key] for key in ('writerEnvelopeSchema','writerPid','writerProcessCreation100ns') if key in row}
    legacyRootPid=False
    if 'writerEnvelopeSchema' not in row:
        mapped.pop('writerPid',None)
        pid=row.get('pid')
        if filename.startswith('hardware-coordinator.log') and type(pid) is int and 0<pid<(1<<32):mapped['writerPid']=pid;legacyRootPid=True
    value=domain_writer_envelope_record(mapped)
    value['scope']='same-row-Fan-Native-writer-envelope;not-FanHost-or-OS-lifetime'
    value['legacyRootPidUsedAsReportedWriter']=legacyRootPid
    value['detailsOrSubjectPidUsedAsWriter']=False
    return value


class FanNativeWriterEnvelopeEvidence(DomainWriterEnvelopeEvidence):
    CARRIER_RE=r'(?:fan-api|hardware-coordinator)\.log(?:\.(?:prev|previous|\d+))?'
    SCOPE='retained-two-Fan-Native-writer-envelope-observations;not-FanHost-or-OS-lifetime'

    def row_record(self,row,filename):
        return fan_native_writer_envelope_record(row,filename)




def resident_event_volume_evidence(files):
    """Bounded retained event-group volume only; never a rate or lane verdict."""
    import heapq
    threshold, per_file_cap, total_cap = 100, 8, 64
    heap = []
    files_by_lane = collections.Counter()
    structured = text_files = eligible = unsupported = local_omissions = retained_local = ordinal = 0

    def offer(target, item, cap):
        if len(target) < cap:
            heapq.heappush(target, item)
        elif item[:2] > target[0][:2]:
            heapq.heapreplace(target, item)

    for entry in files:
        if entry.get('lane') not in RESIDENT_VOLUME_LANES:
            continue
        files_by_lane[entry['lane']] += 1
        if entry.get('format') != 'json-lines':
            text_files += 1
            continue
        structured += 1
        local = []
        local_eligible = 0
        for event in entry.get('events', ()):
            count = event.get('counts', {}).get('all')
            if type(count) is not int or count < 0:
                unsupported += 1
                continue
            if count < threshold:
                continue
            ordinal += 1
            local_eligible += 1
            offer(local, (count, -ordinal, entry, event), per_file_cap)
        eligible += local_eligible
        retained_local += len(local)
        local_omissions += local_eligible - len(local)
        for item in local:
            offer(heap, item, total_cap)

    def label(value, limit):
        value = value if isinstance(value, str) else '<unnamed>'
        prefix = value[:limit]
        # Parsed JSON can contain escaped lone surrogates. Display bounded
        # escapes without turning the original name into a serializable claim.
        shown = prefix.encode('utf-8', errors='backslashreplace').decode('utf-8')[:limit]
        changed = shown != value
        return shown, {'originalCharacters': len(value), 'displayChanged': changed,
            **({'originalNameSha256Utf8Surrogatepass': hashlib.sha256(value.encode('utf-8', errors='surrogatepass')).hexdigest()} if changed else {})}

    observations = []
    for count, negative_ordinal, entry, event in sorted(heap, key=lambda item: (-item[0], -item[1])):
        filename, file_label = label(entry.get('file'), 1024)
        event_name, event_label = label(event.get('event'), 128)
        counts = event.get('counts', {})
        observations.append({'file': filename, 'event': event_name, 'lane': entry['lane'],
            'archiveEntryOccurrence': entry.get('archiveEntryOccurrence'),
            'containerOnly': entry.get('containerOnly'), 'encounterOrdinal': -negative_ordinal,
            'fileLabel': file_label, 'eventLabel': event_label,
            'counts': {key: counts.get(key) for key in ('all', 'currentPidMatch', 'otherPid', 'pidUnknown')},
            'firstLine': event.get('firstLine'), 'lastLine': event.get('lastLine'),
            'retainedRowByteEvidence': entry.get('_residentVolumeRowBytes', {}).get(event.get('event'),
                {'state': 'not-observed'}),
            'reportedOkFieldEvidence': entry.get('_residentVolumeReportedOk', {}).get(event.get('event'),
                {'state': 'not-observed', 'rowsInspected': None})})
    return {'schemaVersion': 2, 'scope': 'known resident/fault-intended retained carriers;event-group row volume and reported root ok field only',
        'thresholdRows': threshold, 'maxEventsPerRetainedCarrier': per_file_cap, 'maxListedEvents': total_cap,
        'residentFilesObserved': sum(files_by_lane.values()), 'filesByLane': dict(files_by_lane),
        'structuredFilesInspected': structured, 'nonStructuredFilesNotEventInspected': text_files,
        'eligibleEventGroupsObserved': eligible, 'eventGroupsNotListed': eligible - len(observations),
        'perCarrierCapOmissions': local_omissions, 'globalCapOmissions': retained_local - len(observations),
        'unsupportedCountEntries': unsupported, 'observations': observations,
        'selection': 'top8-groups-per-carrier-then-top64-by-row-count;ties-use-encounter-order',
        'maxRetainedCandidateReferences': per_file_cap + total_cap,
        'countsAreUniqueOperations': False, 'frequencyRateComputed': False,
        'laneViolationOrFailureInferred': False, 'businessActionDuplicationInferred': False,
        'currentProcessLifetimeProven': False, 'rawRowsRemoved': False,
        'completeEventHistoryProven': False, 'writerBreakdownRepeatedInSummary': False,
        'readingRules': ['count>=100 is a review threshold,not a high-frequency or failure verdict',
            'reportedOkFieldEvidence reads root ok only:strict true/false,missing,null and nonBoolean stay separate;no nested/status/error-code inference',
            'ok true is a producer report,not device/business success;ok false counts rows,not independent failures;PID partitions do not prove process lifetime',
            'file and duplicate archive occurrences remain independent;no cross-member totals or operation joins',
            'currentPidMatch is an original PID filter,not live/loaded or process-birth proof',
            'detailed,mixed,external and unclassified streams are excluded from this resident-intent view',
            'plain-text carriers have no structured event inspection here;empty selection never certifies compliance',
            'full event names and byPid breakdown remain in the original file inventory;bounded labels carry hashes when changed',
            'retainedRowByteEvidence covers listed parsed object rows only;includes original BOM and line endings where present;uncompressed member bytes,not compression or runtime cost']}


OWNER_TRANSITION_STAGES = ('owner-start','owner-stop','owner-restart','engine-start','engine-stop','owner-result')
OWNER_TRANSITION_STATES = ('legacy-unversioned','null-schema','unsupported-schema','invalid-fields','before-call','after-call')
OWNER_TRANSITION_TRIGGERS = ('app-mount','engine-stop','window-focus','doc-visible','gamepad-restart','gamepad-summon',
                             'input-host-crash','clear-summon-state','native-pad-added','native-pad-removed')


def frontend_owner_transition_record(row):
    """A reported local mirror observation; never infer operation success."""
    if not isinstance(row,dict) or row.get('where')!='gamepad-admission':return None
    value=row.get('trace')
    if not isinstance(value,dict) or not isinstance(value.get('stage'),str) or value['stage'] not in OWNER_TRANSITION_STAGES:return None
    result=dict(state='legacy-unversioned',stage=value['stage'],phase=None,transition=None,trigger=None,reason=None,
                ownerState=None,callReturnedObserved=False,nativeAdmissionProven=False,actionOrDeliveryProven=False)
    if 'ownerTransitionSchema' not in value:return result
    if value['ownerTransitionSchema'] is None:result['state']='null-schema';return result
    if type(value['ownerTransitionSchema']) is not int or value['ownerTransitionSchema']!=1:
        result['state']='unsupported-schema';return result
    result['state']='invalid-fields'
    phase=value.get('phase');transition=value.get('transition');stage=value['stage']
    before={'owner-start':'start','owner-stop':'stop','owner-restart':'restart','engine-stop':'stop'}
    transitions=('start','stop','summon','source-inserted','source-removed')
    valid_pair=(phase=='before-call' and stage in before and transition==before[stage] or
                phase=='after-call' and (stage=='engine-start' and transition=='start' or stage=='owner-result' and transition in transitions))
    if not valid_pair or value.get('trigger') not in OWNER_TRANSITION_TRIGGERS:return result
    if value.get('reason') not in (None,'close','blur','suspend','virtual-target-fault','crash'):return result
    owner=value.get('owner');epoch=value.get('epoch')
    if owner not in ('none','game-consumer','ymcc-frontend') or type(epoch) is not int or not 0<=epoch<=(1<<53)-1:return result
    if any(type(value.get(key)) is not bool for key in ('rearm','sourceAdmission','sourcePresent')):return result
    result.update(state=phase,phase=phase,transition=transition,trigger=value['trigger'],reason=value.get('reason'),
                  ownerState={key:value[key] for key in ('owner','epoch','rearm','sourceAdmission','sourcePresent')},
                  callReturnedObserved=phase=='after-call')
    return result


def frontend_owner_transition_evidence(records):
    counts={state:0 for state in OWNER_TRANSITION_STATES};first=[];last=collections.deque(maxlen=8);total=0
    for line,row in records:
        view=frontend_owner_transition_record(row)
        if view is None:continue
        counts[view['state']]+=1;total+=1
        witness=dict(line=line,**view)
        if len(first)<8:first.append(witness)
        else:last.append(witness)
    return dict(scope='retained-carrier-local-mirror-lifecycle-rows;no-cross-row-or-writer-pairing',rows=total,counts=counts,
                witnesses=first+list(last),omittedWitnesses=max(0,total-len(first)-len(last)),witnessLimit=16,
                missingAfterCallIsFailureProof=False,nativeAdmissionProven=False,actionOrDeliveryProven=False)


INPUT_CHAIN_FRAME_EVENTS=('wake-input-chain-first-frame','wake-input-chain-sustained','input-host-frame-reject','wake-input-chain-first-nonneutral')
INPUT_CHAIN_SEMANTIC_SCOPE='submission-neutralization-flag;not-control-values-or-physical-activity'


def input_chain_frame_record(row):
    """Reported local counts; no inference from absence or sampled epoch."""
    if not isinstance(row,dict) or row.get('event') not in INPUT_CHAIN_FRAME_EVENTS:return None
    result=dict(state='legacy-or-unsupported',event=row['event'],accepted=None,nonNeutralAccepted=None,
                countPowerGeneration=None,sampledHostEpoch=None,semanticNeutral=None,reportedLocalAckTuple=None,
                ackProtocolPowerRelationToCount='not-observed',ackHostEpochRelationToSample='not-observed',
                controlsNonzeroProven=False,physicalActivityProven=False,steamConsumptionProven=False,gameInputProven=False)
    if type(row.get('inputChainEvidenceSchema')) is not int or row['inputChainEvidenceSchema']!=3:return result
    def u64(value):return type(value) is int and 0<=value<=(1<<64)-1
    result['state']='invalid-fields'
    keys=('countPowerGeneration','sampledHostEpoch','acceptedInCurrentGeneration','nonNeutralAcceptedInCurrentGeneration')
    if any(not u64(row.get(k)) for k in keys):return result
    if row['countPowerGeneration']==0 or row['nonNeutralAcceptedInCurrentGeneration']>row['acceptedInCurrentGeneration']:return result
    if row.get('countScope')!='current-power-generation-and-sampled-host-epoch' or row.get('hostEpochScope')!='sampled-at-count-not-frame-tuple-proof':return result
    if row.get('evidenceScope')!='host-only' or row.get('gameInputUnverified') is not True:return result
    edge_schema=None
    if row['event']=='wake-input-chain-first-nonneutral':
        if 'nonNeutralEvidenceSchema' not in row:result['state']='missing-edge-schema';return result
        edge_schema=row['nonNeutralEvidenceSchema']
        if type(edge_schema) is not int or edge_schema not in (1,2):
            result['state']='unsupported-edge-schema';return result
        if row.get('semanticNeutral') is not False or row.get('semanticScope')!=INPUT_CHAIN_SEMANTIC_SCOPE or row['nonNeutralAcceptedInCurrentGeneration']!=1:return result
        if edge_schema==1 and row.get('ackIdentityStatus')!='not-projected;count-scope-only':return result
    neutral=row.get('semanticNeutral') if type(row.get('semanticNeutral')) is bool else None
    result.update(state='reported-local-counts',accepted=row['acceptedInCurrentGeneration'],nonNeutralAccepted=row['nonNeutralAcceptedInCurrentGeneration'],
                  countPowerGeneration=row['countPowerGeneration'],sampledHostEpoch=row['sampledHostEpoch'],semanticNeutral=neutral)
    if edge_schema==2:
        keys=('protocolPowerGeneration','hostEpoch','wireSequence','runId','hostInstanceId','requestId')
        fields=('ackProtocolPowerGeneration','ackHostEpoch','ackWireSequence','ackRunId','ackHostInstanceId','ackRequestId')
        ack={k:row.get(f) for k,f in zip(keys,fields)}
        origin=row.get('ackIdentityOrigin');status=row.get('ackIdentityStatus');wire=row.get('wireSequence')
        result['reportedAckStatus']='invalid-fields'
        if status=='unknown' and 'ackIdentityOrigin' not in row and not any(f in row for f in fields):
            result['reportedAckStatus']='unknown'
        elif (status=='verified-local-ack' and origin in ('strict-fast-validator-equality-and-parsed-host','existing-validator-and-parsed-dom-reply')
              and all(u64(ack[k]) and ack[k]>0 for k in keys[:3]) and all(isinstance(ack[k],str) and ack[k] for k in keys[3:])
              and u64(wire) and (wire==ack['wireSequence'] or (wire==0 and origin=='existing-validator-and-parsed-dom-reply'))):
            result.update(reportedAckStatus=status,reportedAckOrigin=origin,reportedLocalAckTuple=ack)
    elif row['event']!='wake-input-chain-first-nonneutral':
        ack=row.get('ackIdentity')
        if isinstance(ack,dict) and type(ack.get('schema')) is int and ack['schema']==1 and ack.get('authority')=='native-local-ack-diagnostic' and ack.get('status')=='verified-local-ack' and ack.get('nativeAccepted') is True and ack.get('tupleVerified') is True:
            if all(u64(ack.get(k)) and ack[k]>0 for k in ('protocolPowerGeneration','hostEpoch','wireSequence')) and all(isinstance(ack.get(k),str) and ack[k] for k in ('runId','hostInstanceId','requestId')):
                result['reportedLocalAckTuple']={k:ack[k] for k in ('protocolPowerGeneration','hostEpoch','wireSequence','runId','hostInstanceId','requestId')}
    ack=result['reportedLocalAckTuple']
    if ack is not None:
        result['ackProtocolPowerRelationToCount']='same' if ack['protocolPowerGeneration']==result['countPowerGeneration'] else 'different'
        if result['sampledHostEpoch']>0:
            result['ackHostEpochRelationToSample']='same' if ack['hostEpoch']==result['sampledHostEpoch'] else 'different'
    return result


def input_chain_frame_evidence(records):
    counts=collections.Counter();power=collections.Counter();host=collections.Counter();first=[];last=collections.deque(maxlen=8);total=0
    for line,row in records:
        view=input_chain_frame_record(row)
        if view is None:continue
        total+=1;counts[view['state']]+=1;witness=dict(line=line,**view)
        power[view['ackProtocolPowerRelationToCount']]+=1
        host[view['ackHostEpochRelationToSample']]+=1
        if len(first)<8:first.append(witness)
        else:last.append(witness)
    return dict(scope='retained-local-count-rows;no-cross-row-or-writer-join;no-absence-inference',rows=total,counts=dict(counts),
                ackProtocolPowerRelationsToCount=dict(power),ackHostEpochRelationsToSample=dict(host),
                ackRelationScope='same-row-value-comparison-only;not-current-tuple-or-delivery-proof',
                witnesses=first+list(last),omittedWitnesses=max(0,total-len(first)-len(last)),witnessLimit=16,
                missingIsZeroOrFailureProof=False,steamConsumptionProven=False,gameInputProven=False)


WAKE_EXECUTION_MEMBERS = {
    'native-lifecycle.log': 'base', 'native-lifecycle.log.1': 'base',
    'native-detail.log': 'detail', 'native-detail.log.1': 'detail',
}


def wake_execution_evidence(members, occurrences):
    """Join bounded original receipts within one ZIP, using the producer's wake identity."""
    maximum = 32
    operations, excluded = {}, collections.Counter()
    wait_reason = 'kept-target-waiting-for-physical-return'
    ambiguous = [name for name in WAKE_EXECUTION_MEMBERS if occurrences[name] > 1]

    def uint(value, bits=64, minimum=0):
        return type(value) is int and minimum <= value < (1 << bits)

    for name, rows, metadata in members:
        lane = WAKE_EXECUTION_MEMBERS.get(name)
        if lane is None or occurrences[name] != 1 or metadata.get('containerOnly') is not False:
            continue
        for line, row in rows:
            event = row.get('event')
            refresh = event == 'input-host-wake-hot-replace' and row.get('action') == 'hide-refreshed'
            if refresh:
                if lane != 'base' or type(row.get('ok')) is not bool:
                    excluded['bad-refresh-receipt'] += 1;continue
                generation, epoch = row.get('wakePowerGeneration'), row.get('queuedEpoch')
                kind = 'refresh-ok' if row['ok'] else 'refresh-not-ok'
            elif event in ('input-host-wake-execution', 'wake-physical-return-wait-sample'):
                detail = event == 'wake-physical-return-wait-sample'
                stage, reason = row.get('stage'), row.get('reason')
                if ((lane == 'detail') != detail or row.get('role') != 'normal-power-execution' or
                        row.get('lifecycleVersion') != 'VC-LIFECYCLE-20261020-R3' or
                        stage not in ('armed', 'duplicate', 'retained', 'complete', 'retired')):
                    excluded['unsupported-execution-receipt'] += 1;continue
                waiting = stage == 'retained' and reason == wait_reason
                if (detail and not waiting) or (waiting and not uint(row.get('physicalReturnWaits'), 32, 1)):
                    excluded['bad-physical-wait-receipt'] += 1;continue
                generation, epoch = row.get('producerPowerGeneration'), row.get('hostEpoch')
                kind = 'physical-wait-' + lane if waiting else stage
            else:
                continue
            pid, birth, serial, request = (row.get(key) for key in
                ('pid', 'writerProcessCreation100ns', 'wakeSerial', 'requestId'))
            if (not uint(pid, 32, 1) or type(birth) is not str or not re.fullmatch(r'[1-9][0-9]{0,19}', birth)
                    or int(birth) >= 1 << 64 or not all(uint(value, minimum=1) for value in (serial, request, generation))):
                excluded['refresh-identity-unavailable' if refresh else 'execution-identity-unavailable'] += 1;continue
            key = (pid, birth, serial, request, generation)
            if key not in operations:
                if len(operations) >= maximum:
                    excluded['operation-limit-rows'] += 1;continue
                operations[key] = {'identity': dict(zip(('writerPid', 'writerProcessCreation100ns', 'wakeSerial',
                    'requestId', 'operationPowerGeneration'), key)), 'countsByKind': {}, 'firstByKind': {}}
            group = operations[key]
            counts = group['countsByKind']
            counts[kind] = counts.get(kind, 0) + 1
            if kind in group['firstByKind']:
                continue
            witness = {'file': name, 'line': line, 'hostEpoch': epoch if uint(epoch) else None,
                       'observedPowerGeneration': row['powerGeneration'] if uint(row.get('powerGeneration')) else None}
            if not refresh:
                witness['reason'] = reason if type(reason) is str and 0 < len(reason) <= 128 else None
                for field in ('keptHideFailures', 'physicalReturnWaits', 'recreateAttempts', 'startAttempts'):
                    witness[field] = row[field] if uint(row.get(field), 32) else None
            group['firstByKind'][kind] = witness

    for group in operations.values():
        counts = group['countsByKind']
        terminals = counts.get('complete', 0) + counts.get('retired', 0)
        group['terminalState'] = ('ambiguous-retained-terminal-rows' if terminals > 1 else
            'reported-local-complete' if counts.get('complete') else 'reported-retired' if counts.get('retired') else 'not-observed')
        group['terminal'] = (group['firstByKind']['complete' if counts.get('complete') else 'retired']
            if terminals == 1 else None)
    return {'scope': 'same-zip-exact-writer-wake-request-producer-power;host-epochs-are-row-observations',
            'operations': list(operations.values()), 'maxOperations': maximum, 'excludedRows': dict(excluded),
            'ambiguousMembers': ambiguous, 'missingIsZeroOrFailureProof': False, 'gameInputProven': False}


OVERLAY_ENTRY_COUNTERS=('activePass','activeReject','inactivePass','inactiveReject')


def renderer_overlay_session_entry_evidence(availability):
    result=dict(state='not-observed',counts={k:None for k in OVERLAY_ENTRY_COUNTERS},atLimit={k:None for k in OVERLAY_ENTRY_COUNTERS},
                counterFieldsKnown=0,known=False,scope='reported-renderer-completed-session-gate-only;no-cross-input-or-Native-join',
                nativeAdmissionProven=False,actionOrDeliveryProven=False,zeroIsNoInputProof=False)
    if not isinstance(availability,dict):return result
    delivery=availability.get('rendererDiagnosticDeliveryEvidence')
    if not isinstance(delivery,dict) or 'overlaySessionEntry' not in delivery:return result
    value=delivery['overlaySessionEntry']
    if value is None:result['state']='reported-null';return result
    if not isinstance(value,dict):result['state']='invalid-shape';return result
    if type(value.get('schema')) is not int or value['schema']!=1:result['state']='unsupported-schema';return result
    if value.get('scope')!='renderer-reported-session-entry-counts-only':result['state']='unsupported-scope';return result
    states=('not-reported','reported-null','invalid-shape','unsupported-schema','unsupported-scope-or-limit','projected','partial-counters')
    status=value.get('status')
    if status not in states:result['state']='unsupported-status';return result
    if status not in ('projected','partial-counters'):result['state']=status;return result
    result['state']='invalid-fields'
    if type(value.get('counterLimit')) is not int or value['counterLimit']!=(1<<53)-1:return result
    counts=value.get('counts');limits=value.get('atLimit')
    if not isinstance(counts,dict) or not isinstance(limits,dict):return result
    parsed={};at_limit={};known=0
    for key in OVERLAY_ENTRY_COUNTERS:
        n=counts.get(key)
        if n is None:
            if limits.get(key) is not None:return result
            parsed[key]=None;at_limit[key]=None;continue
        if type(n) is not int or not 0<=n<=(1<<53)-1 or type(limits.get(key)) is not bool or limits[key]!=(n==(1<<53)-1):return result
        parsed[key]=n;at_limit[key]=limits[key];known+=1
    if type(value.get('counterFieldsKnown')) is not int or value['counterFieldsKnown']!=known or type(value.get('known')) is not bool or value['known']!=(known==4) or (status=='projected')!=(known==4):return result
    result.update(state=status,counts=parsed,atLimit=at_limit,counterFieldsKnown=known,known=known==4)
    return result


EXEC_GUARD_FIELDS=('overlaySessionMatch','epochMatch','loopAllowed','childOwned')
EXEC_GUARD_SCOPE='original-short-circuit-at-execution;null=not-evaluated'
EXEC_GUARD_PATTERNS=((False,None,None,None),(True,False,None,None),(True,True,False,None),(True,True,True,True))
EXEC_GUARD_FAILURES=('overlay-session','owner-epoch','browser-loop','child-owner')


def frontend_exec_guard_record(row):
    if not isinstance(row,dict) or row.get('where')!='gamepad-admission':return None
    trace=row.get('trace')
    if not isinstance(trace,dict) or trace.get('stage')!='exec-drop':return None
    result=dict(state='legacy-missing',guard=None,firstFailure=None,known=False,actionOrDeliveryProven=False,nativeAdmissionProven=False)
    if 'execGuard' not in trace:return result
    guard=trace['execGuard']
    if guard is None:result['state']='reported-null';return result
    if not isinstance(guard,dict):result['state']='invalid-shape';return result
    if type(guard.get('schema')) is not int or guard['schema']!=1:result['state']='unsupported-schema';return result
    if guard.get('scope')!=EXEC_GUARD_SCOPE:result['state']='unsupported-scope';return result
    result['state']='invalid-fields'
    if any(k not in guard or guard[k] is not None and type(guard[k]) is not bool for k in EXEC_GUARD_FIELDS):return result
    values=tuple(guard[k] for k in EXEC_GUARD_FIELDS)
    # Child ownership rejects when true;all earlier predicates reject when false.
    if values not in EXEC_GUARD_PATTERNS:return result
    index=EXEC_GUARD_PATTERNS.index(values)
    result.update(state='reported-first-failure',guard={k:guard[k] for k in EXEC_GUARD_FIELDS},firstFailure=EXEC_GUARD_FAILURES[index],known=True)
    return result


def frontend_exec_guard_evidence(records):
    counts=collections.Counter();first=[];last=collections.deque(maxlen=8);total=0
    for line,row in records:
        view=frontend_exec_guard_record(row)
        if view is None:continue
        total+=1;counts[view['state']]+=1;witness=dict(line=line,**view)
        if len(first)<8:first.append(witness)
        else:last.append(witness)
    return dict(scope='retained-exec-drop-original-first-predicate-failure;no-cross-row-join',rows=total,counts=dict(counts),
                witnesses=first+list(last),witnessLimit=16,omittedWitnesses=max(0,total-len(first)-len(last)),
                missingIsUnknown=True,actionOrDeliveryProven=False,nativeAdmissionProven=False)


class UiActionLinkEvidence:
    MAX_KEYS = 4096
    STAGES = ('entry-pass', 'entry-drop', 'exec-drop', 'exec-dedup', 'exec-run')
    NAVIGATION_STAGES = ('nav-not-started', 'nav-start', 'nav-done', 'nav-rejected', 'nav-fail')
    SHARED_HID_ACTIONS = frozenset(('confirm', 'back', 'edit-game', 'nav-up', 'nav-down', 'nav-left', 'nav-right'))

    @classmethod
    def shared_hid_sample(cls, detail, action):
        sample = detail.get('sharedHidSample')
        if (action not in cls.SHARED_HID_ACTIONS or not isinstance(sample, dict) or
                set(sample) != {'schema', 'reportSequence', 'reportObservedTick'} or
                type(sample['schema']) is not int or sample['schema'] != 1 or
                any(type(sample[k]) is not int or not 0 < sample[k] < 1 << 64
                    for k in ('reportSequence', 'reportObservedTick'))):
            return None
        return sample

    def __init__(self):
        self.counts = collections.Counter()
        self.actions = {}

    def observe(self, name, rows, metadata):
        base = canonical_log_name(name)
        native = base in ('native-lifecycle.log', 'native-detail.log')
        if not native and base not in ('frontend-trace.log', 'frontend-trace-detail.log'):
            return
        for line, row in rows:
            if native:
                if row.get('event') != 'gamepad-ui-input-emitted' or row.get('target') != 'yemancc':
                    continue
                stage, detail, pid = 'native-before-send', row, row.get('pid')
            else:
                detail = row.get('trace')
                if row.get('event') != 'frontend-trace' or row.get('where') != 'gamepad-admission' or not isinstance(detail, dict):
                    continue
                stage, pid = detail.get('stage'), row.get('writerPid')
                if stage not in self.STAGES and stage not in self.NAVIGATION_STAGES:
                    continue
                if type(row.get('writerEnvelopeSchema')) is not int or row['writerEnvelopeSchema'] != 1:
                    pid = None
            link = detail.get('uiActionLink')
            navigation = stage in self.NAVIGATION_STAGES
            self.counts['nativeRows' if native else 'navigationRows' if navigation else 'frontendRows'] += 1
            if 'uiActionLink' not in detail:
                self.counts['legacyNavigationRowsWithoutLink' if navigation else 'legacyRowsWithoutLink'] += 1
                continue
            birth, action = row.get('writerProcessCreation100ns'), detail.get('action')
            session = link.get('session') if isinstance(link, dict) else None
            seq = link.get('sequence') if isinstance(link, dict) else None
            if (not isinstance(link, dict) or set(link) != {'schema', 'session', 'sequence'} or
                    type(link['schema']) is not int or link['schema'] != 1 or
                    type(pid) is not int or not 0 < pid <= 0xffffffff or
                    not isinstance(birth, str) or not re.fullmatch(r'[1-9][0-9]{0,19}', birth) or int(birth) >= 1 << 64 or
                    not isinstance(session, str) or not re.fullmatch(r'[1-9][0-9]{0,9}:[1-9][0-9]{0,19}', session) or
                    session.split(':')[0] != str(pid) or int(session.split(':')[1]) >= 1 << 64 or
                    type(seq) is not int or not 0 < seq < 1 << 53 or
                    not isinstance(action, str) or not re.fullmatch(r'[a-z][a-z-]{0,31}', action) or
                    (navigation and action not in ('page-prev', 'page-next'))):
                self.counts['invalidLinkOrWriterRows'] += 1
                continue
            key = pid, birth, session, seq
            if key not in self.actions and len(self.actions) == self.MAX_KEYS:
                self.counts['rowsBeyondKeyLimit'] += 1
                continue
            item = self.actions.setdefault(key, {'action': action, 'nativeCount': 0, 'actionsAgree': True,
                                                  'stages': collections.Counter(), 'witnesses': {}})
            item['actionsAgree'] &= item['action'] == action
            if native:
                item['nativeCount'] += 1
                if 'sharedHidSample' in detail:
                    sample = self.shared_hid_sample(detail, action)
                    if sample is None:
                        self.counts['nativeRowsWithInvalidSharedHidSample'] += 1
                    else:
                        self.counts['nativeRowsWithValidSharedHidSample'] += 1
                        if item['nativeCount'] == 1:
                            item['sharedHidSample'] = sample
            else:
                item['stages'][stage] += 1
            if stage not in item['witnesses']:
                witness = {'file': name[:128], 'line': line,
                           'archiveEntryOccurrence': metadata.get('archiveEntryOccurrence')}
                if stage == 'nav-not-started':
                    gate, reason = detail.get('gate'), detail.get('reason')
                    if (gate in ('before-queue', 'after-queue') and
                            (reason in ('modal-open', 'page-lock') or
                             (gate == 'before-queue' and reason == 'page-command-handled'))):
                        witness.update(gate=gate, reason=reason)
                elif stage == 'nav-rejected':
                    code = detail.get('routerFailure')
                    if type(code) is int and 0 < code <= 0xffffffff:
                        witness['routerFailure'] = code
                elif stage == 'nav-fail' and detail.get('failureAt') in ('router-push', 'after-push'):
                    witness['failureAt'] = detail['failureAt']
                item['witnesses'][stage] = witness

    def evidence(self):
        total = collections.Counter(); sample_keys = collections.Counter(); examples = []
        for (pid, birth, session, seq), entry in self.actions.items():
            native, stages = entry['nativeCount'], entry['stages']
            if native > 1 or not entry['actionsAgree']:
                state = 'ambiguous'
            elif native == 1 and stages:
                state = 'matched-reported-stages'
            else:
                state = 'native-only' if native else 'frontend-only'
            total[state] += 1
            sample = entry.get('sharedHidSample') if state != 'ambiguous' else None
            if sample is not None:
                sample_keys[state] += 1
            if len(examples) < 16:
                examples.append({'state': state, 'writerPid': pid, 'writerProcessCreation100ns': birth,
                                 'session': session, 'sequence': seq, 'action': entry['action'], 'nativeRows': native,
                                 'frontendStages': dict(stages), 'firstWitnessByStage': entry['witnesses']})
                if sample is not None:
                    examples[-1]['sharedHidSample'] = sample
        return {'schema': 1, 'scope': 'exact-reported-ui-identity;no-timestamp-or-cross-process-join',
                'maxKeys': self.MAX_KEYS, 'maxExamples': 16, 'counts': dict(self.counts),
                'keysByState': dict(total), 'examples': examples, 'missingStageIsUnknown': True,
                'execRunMeansBeforeDispatch': True, 'navigationStagesAreRendererReported': True,
                'keysWithSharedHidSampleByState': dict(sample_keys),
                'sharedHidSampleScope': 'native-immediate-action-evaluation;missing-is-unknown;no-freshness-or-consumption-proof',
                'postAcceptedOrNavigationProven': False}


ORDINARY_EXPORT_OPERATION_FIELDS=frozenset(('schema','scope','id','writerPid','writerProcessCreation100ns','ordinal','startedTickMs'))


def ordinary_export_terminal_record(row):
    """One reported Native terminal row;field values never certify delivery."""
    result=dict(reported={},fieldIssues=[],phaseTiming=export_phase_timing_record(row),terminalStageState='not-observed',
                windowsPhaseFractionOfExport=None,archiveCallFractionOfExport=None,
                wholeExportOrCpuProof=False,zipOrCleanupDurabilityProven=False)
    for key in ('ok','cleanupOk','bodyReturned','attempts','exportElapsedMs','archiveElapsedMs','phaseTimingBuildFailures','reason','completion','terminalStage','exportMode'):
        if key not in row:continue
        value=row[key]
        valid=(type(value) is bool if key in ('ok','cleanupOk','bodyReturned') else
               type(value) is int and 0<=value<=2 if key=='attempts' else
               type(value) is int and 0<=value<(1<<64) if key in ('exportElapsedMs','archiveElapsedMs','phaseTimingBuildFailures') else
               value in ('quick','full') if key=='exportMode' else isinstance(value,str) and len(value)<=256)
        if valid:result['reported'][key]=value
        else:result['fieldIssues'].append(key+':invalid-type-range-or-length')
    stage=result['reported'].get('terminalStage');body=result['reported'].get('bodyReturned')
    known=('before-archive-return','archive-return','body-exception-before-archive','body-exception-after-archive')
    if stage is not None or body is not None:
        result['terminalStageState']='coherent-reported-stage' if stage in known and type(body) is bool and body==('exception' not in stage) else 'missing-unsupported-or-conflicting-stage'
    total=result['reported'].get('exportElapsedMs');archive=result['reported'].get('archiveElapsedMs')
    windows=result['phaseTiming']['elapsedMs'].get('windowsSystemPowerReport')
    if type(total) is int and total>0:
        if type(windows) is int and windows<=total:result['windowsPhaseFractionOfExport']=dict(numeratorMs=windows,denominatorMs=total)
        elif type(windows) is int:result['fieldIssues'].append('windows-phase:exceeds-reported-export-wall')
        if type(archive) is int and archive<=total:result['archiveCallFractionOfExport']=dict(numeratorMs=archive,denominatorMs=total)
        elif type(archive) is int:result['fieldIssues'].append('archive-call:exceeds-reported-export-wall')
    return result


def ordinary_export_manifest_mode(fields, selection):
    status = selection['status']
    mode = fields.get('logExportMode')
    occurrences = selection.get('ambiguousFieldOccurrences', {}).get('logExportMode', 0)
    if occurrences:
        state = 'ambiguous-field'
    elif status not in ('selected', 'selected-with-ambiguous-fields'):
        state = 'manifest-unavailable'
    elif mode is None:
        state = 'missing-field'
    elif mode not in ('quick', 'full'):
        state = 'unsupported-value'
    else:
        state = 'reported'
    return dict(state=state, reportedMode=mode if state == 'reported' else None,
                selectionStatus=status, archiveEntryOccurrence=selection['selectedOccurrence'],
                ambiguousFieldOccurrences=occurrences)


def ordinary_export_mode_evidence(manifest, transaction, terminal, terminal_association):
    """Keep receipt-archive mode, query disposition and exact terminal mode separate."""
    query = transaction['windowsQuery'] or {}
    query_state = query.get('state', 'receipt-unavailable')
    outcome = query.get('outcomeAndTiming', {})
    current = outcome.get('current', {})
    disposition = 'not-established'
    if query_state == 'reported-skipped-by-request':
        disposition = 'skipped-by-request'
    elif query_state == 'reported' and outcome.get('state') == 'reported-code-and-time-domains':
        if current.get('state') == 'suppressed-current-query-not-executed':
            disposition = 'suppressed-recent-deadline'
        elif current.get('state') == 'reported-code-and-time-domains':
            disposition = 'current-query-attempt-reported'
    operation_bound = (transaction['operation']['state'] == 'reported-process-operation' and
                       transaction['state'] != 'operation-unsupported-fields')
    terminal_mode = terminal['reported'].get('exportMode') if terminal is not None else None
    terminal_state = ('not-selected' if terminal is None else 'reported' if terminal_mode is not None else
                      'invalid-field' if 'exportMode:invalid-type-range-or-length' in terminal['fieldIssues'] else
                      'missing-field')
    manifest_mode = manifest['reportedMode']
    conflicts = []
    if manifest_mode is not None and terminal_mode is not None and manifest_mode != terminal_mode:
        conflicts.append('manifest-vs-terminal')
    if operation_bound:
        incompatible = {'skipped-by-request': 'full', 'suppressed-recent-deadline': 'quick',
                        'current-query-attempt-reported': 'quick'}.get(disposition)
        if incompatible is not None:
            if manifest_mode == incompatible:
                conflicts.append('manifest-vs-windows-query')
            if terminal_mode == incompatible:
                conflicts.append('terminal-vs-windows-query')
    return dict(manifest=manifest, windowsQuery=dict(receiptState=query_state, disposition=disposition,
                    supportedReceiptOperation=operation_bound),
                terminal=dict(state=terminal_state, reportedMode=terminal_mode, association=terminal_association),
                conflicts=conflicts)


def cooldown_origin_operation(recent, target):
    origin_id=recent.get('originalExportId')
    completed=recent.get('completedTickMs')
    if not origin_id or completed is None:
        return 'origin-reference-incomplete', None
    parts=origin_id.split('-')
    if len(parts)!=3 or parts[0]!=str(target['writerPid']):
        return 'origin-reference-conflict', None
    expected=dict(target,id=origin_id,startedTickMs=parts[1],ordinal=parts[2])
    if (export_operation_record({'exportOperation':expected})['state']!='reported-process-operation' or
            int(expected['ordinal'])>=int(target['ordinal']) or
            not int(expected['startedTickMs'])<=int(completed)<=int(target['startedTickMs'])):
        return 'origin-reference-conflict', None
    return 'origin-reference-reported', expected


def cooldown_export_origin_reference(windows, target, result):
    if windows['reported'].get('querySuppressed') is not True:
        result['state']='no-reported-cooldown'
        return None
    if windows['state']!='reported':
        result['state']='cooldown-receipt-invalid'
        return None
    recent=windows['recentDeadline']
    state, expected = cooldown_origin_operation(recent, target)
    if state != 'origin-reference-incomplete':
        result['originalExportId'] = recent['originalExportId']
    if expected is None:
        result['state'] = state
        return None
    result.update(state='origin-terminal-not-observed',candidateRows=0)
    return expected


def prior_final_output_after_move(value, attempts, archive_ok):
    result = dict(state='invalid-fields', reported={}, uniqueFinalFileOrDurableCleanupProven=False)
    if value is None:
        return dict(result, state='null')
    fields = {'schema', 'attempt', 'state', 'sizeBytes'}
    if not isinstance(value, dict) or not fields <= value.keys() <= fields | {'openWin32Error'}:
        return result
    if type(value['schema']) is not int or value['schema'] != 1:
        return dict(result, state='unsupported-schema')
    if type(value['attempt']) is not int or value['attempt'] not in (1, 2) or value['attempt'] != attempts:
        return dict(result, state='unexpected-attempt')
    state, size = value['state'], value['sizeBytes']
    if state in ('stream-opened-nonempty', 'stream-open-failed'):
        valid = type(size) is int and 0 < size < 1 << 64
    elif state == 'empty-file':
        valid = type(size) is int and size == 0
    elif state in ('file-status-error', 'file-size-error', 'not-regular-file'):
        valid = size is None
    else:
        valid = False
    if 'openWin32Error' in value:
        error = value['openWin32Error']
        valid = valid and state == 'stream-open-failed' and type(error) is int and 0 < error < 1 << 32
    if valid and archive_ok is (state == 'stream-opened-nonempty'):
        result.update(state='reported-final-check-after-move', reported=dict(value))
    return result


def reported_final_output_after_move(timing, attempts, archive_ok):
    if timing['state'] == 'missing':
        return 'raw-final-check-not-observed', None
    rows = timing['attempts']
    if (timing['state'] != 'reported-completed-steps' or type(attempts) is not int or attempts not in (1, 2) or
            len(rows) != attempts or type(archive_ok) is not bool):
        return 'raw-final-check-unusable', None
    last = rows[attempts - 1]
    output, promotion = last['outputCheck'], last['promotion']
    if output['state'] == 'missing' or promotion['state'] == 'missing':
        return 'raw-final-check-not-observed', None
    if (last['reported'].get('attemptBodyReturned') is not True or
            output['state'] != 'reported-original-output-checks' or
            promotion['state'] != 'reported-original-promotion-observations'):
        return 'raw-final-check-unusable', None
    move, check = promotion['promotion'], promotion['postPromotionValidation']
    if move.get('accepted') is not True or check.get('readableAndNonempty') is not archive_ok:
        return 'reported-fields-conflict', None
    final = output['checks']['final']
    raw = {'schema': 1, 'attempt': attempts, **{k: final[k] for k in ('state', 'sizeBytes', 'openWin32Error') if k in final}}
    return 'reported-final-check-after-move', raw


def compare_prior_final_output_after_move(carried, timing, attempts, archive_ok):
    state, original = reported_final_output_after_move(timing, attempts, archive_ok)
    if original is None:
        return state
    return 'reported-fields-agree' if carried == original else 'reported-fields-conflict'


def prior_archive_steps_record(value, count, archive_ms):
    result = dict(state='invalid-fields', attempts=[], retainedRowComparison='not-compared',
                  scope='preceding-archive-call-completed-step-wall-ms;not-additional-export-time')
    required = {'schema', 'attempts'}
    if (not isinstance(value, dict) or not required <= value.keys() <= required | {'childFailures', 'failedTemporaryLookups'} or
            type(value['schema']) is not int or value['schema'] != 1 or
            type(count) is not int or count not in (1, 2) or type(archive_ms) is not int or not 0 <= archive_ms < 1 << 64 or
            not isinstance(value['attempts'], list) or len(value['attempts']) != count):
        return result
    remaining = archive_ms
    for number, attempt in enumerate(value['attempts'], 1):
        if (not isinstance(attempt, dict) or attempt.keys() != {'attempt', 'totalElapsedMs', 'completedStepElapsedMs'} or
                type(attempt['attempt']) is not int or attempt['attempt'] != number):
            return result
        total, phases = attempt['totalElapsedMs'], attempt['completedStepElapsedMs']
        if (type(total) is not int or not 0 <= total <= remaining or not isinstance(phases, dict) or
                not phases.keys() <= set(ARCHIVE_ATTEMPT_TIMING_PHASES) or
                (number == 2 and 'retryDelay' in phases) or
                any(type(ms) is not int or not 0 <= ms <= total for ms in phases.values()) or
                sum(phases.values()) > total):
            return result
        remaining -= total
    breakdown = archive_wall_breakdown(archive_ms, count, [
        (attempt['totalElapsedMs'], attempt['completedStepElapsedMs']) for attempt in value['attempts']])
    return dict(result, state='reported-completed-steps', attempts=value['attempts'], wallBreakdown=breakdown)


def prior_archive_child_failures(value, steps, ok):
    invalid = dict(state='invalid-fields', failures=[], retainedRowComparison='not-compared')
    attempts = steps['attempts']
    if (steps['state'] != 'reported-completed-steps' or type(ok) is not bool or
            not isinstance(value, list) or not 1 <= len(value) <= len(attempts) - int(ok)):
        return invalid
    previous = 0
    for row in value:
        if not isinstance(row, dict) or row.keys() != {'attempt', 'zipCompressionRequested', 'archiveChildFailure'}:
            return invalid
        number = row['attempt']
        if type(number) is not int or not previous < number <= len(attempts) - int(ok):
            return invalid
        if archive_child_failure_record(row, attempts[number - 1]['completedStepElapsedMs'])['state'] != 'reported-original-child-failure':
            return invalid
        previous = number
    return dict(invalid, state='reported-archive-child-failures', failures=value)


def compare_prior_archive_child_failures(carried, timing):
    if timing['state'] == 'missing':
        return 'raw-child-failure-not-observed'
    if timing['state'] != 'reported-completed-steps':
        return 'raw-child-failure-unusable'
    missing = False
    for row in carried:
        index = row['attempt'] - 1
        if index >= len(timing['attempts']):
            return 'reported-fields-conflict'
        original = timing['attempts'][index]
        failure = original.get('archiveChildFailure')
        if failure is None:
            missing = True
        elif failure['state'] != 'reported-original-child-failure':
            return 'raw-child-failure-unusable'
        elif (original['reported'].get('zipCompressionRequested') != row['zipCompressionRequested'] or
                failure['reported'] != row['archiveChildFailure']):
            return 'reported-fields-conflict'
    return 'raw-child-failure-not-observed' if missing else 'reported-fields-agree'


def prior_archive_temporary_lookups(value, steps, ok, cleanup_ok):
    result = dict(state='invalid-fields', lookups=[], retainedRowComparison='not-compared')
    attempts = steps['attempts']
    if (steps['state'] != 'reported-completed-steps' or type(ok) is not bool or cleanup_ok is not False or
            not isinstance(value, list) or not 1 <= len(value) <= len(attempts) - int(ok)):
        return result
    previous = 0
    for row in value:
        if not isinstance(row, dict) or row.keys() != {'attempt', 'failedCleanupErrors', 'failedTemporaryAfterCleanup'}:
            return result
        number, errors = row['attempt'], row['failedCleanupErrors']
        if (type(number) is not int or not previous < number <= len(attempts) - int(ok) or
                not isinstance(errors, list) or len(errors) != 2 or
                any(not isinstance(e, dict) or e.keys() != {'errorCategory', 'errorValue'} or
                    e['errorCategory'] not in ('system', 'generic', 'other') or type(e['errorValue']) is not int or
                    not -(1 << 31) <= e['errorValue'] < (1 << 31) for e in errors) or
                not any(e['errorValue'] for e in errors)):
            return result
        elapsed = attempts[number - 1]['completedStepElapsedMs'].get('attemptCleanup')
        if archive_temporary_lookup_record(row['failedTemporaryAfterCleanup'], elapsed, True)['state'] != 'reported-owned-temporary-observation':
            return result
        previous = number
    return dict(result, state='reported-archive-temporary-lookups', lookups=value)


def compare_prior_archive_temporary_lookups(lookups, timing):
    if timing['state'] == 'missing':
        return 'raw-temporary-lookup-not-observed'
    if timing['state'] != 'reported-completed-steps':
        return 'raw-temporary-lookup-unusable'
    missing = False
    for row in lookups:
        i = row['attempt'] - 1
        if i >= len(timing['attempts']):
            return 'reported-fields-conflict'
        original = timing['attempts'][i]
        lookup = original.get('failedTemporaryAfterCleanup')
        if lookup is None:
            missing = True
            continue
        if lookup['state'] != 'reported-owned-temporary-observation':
            return 'raw-temporary-lookup-unusable'
        sites = {site['site']: site for site in original['cleanup']['sites']}
        errors = [{k: sites[site][k] for k in ('errorValue', 'errorCategory')} for site in FAILED_CLEANUP_SITE_NAMES]
        carried = row['failedTemporaryAfterCleanup']
        if errors != row['failedCleanupErrors'] or any(carried[k] != v for k, v in lookup['reported'].items()):
            return 'reported-fields-conflict'
    return 'raw-temporary-lookup-not-observed' if missing else 'reported-fields-agree'


def compare_prior_archive_steps(carried, timing):
    if timing['state'] == 'missing':
        return 'raw-steps-not-observed'
    rows = timing['attempts']
    if (timing['state'] != 'reported-completed-steps' or
            any(row['unrecognizedPhaseCount'] or row['reported'].get('attemptBodyReturned') is not True for row in rows)):
        return 'raw-steps-unusable'
    original = [dict(attempt=row['reported']['attempt'], totalElapsedMs=row['reported']['totalElapsedMs'],
                     completedStepElapsedMs=row['completedStepElapsedMs']) for row in rows]
    return 'reported-fields-agree' if carried == original else 'reported-fields-conflict'


PRIOR_COPY_FAILURE_STATES = ('reported-complete-observed-failure-coverage',
                             'reported-incomplete-observed-failure-coverage')


def prior_snapshot_copy_failure_record(row):
    """Only the bounded original mandatory-copy payload, with its own coverage."""
    field = 'snapshotCopyFailureEvidence'
    checked = export_copy_failure_record(row)
    state = checked['state']
    result = dict(state=state, value=None, fieldIssues=checked['fieldIssues'])
    if state not in PRIOR_COPY_FAILURE_STATES:
        return result
    value, reported = row[field], checked['reported']
    attempts, ok = row.get('attempts'), row.get('ok')
    if type(attempts) is not int or attempts not in (1, 2) or type(ok) is not bool:
        return dict(result, state='invalid-fields', fieldIssues=['archive-outcome-or-attempt-count-unavailable'])
    if reported['failureObservations'] > attempts - int(ok):
        return dict(result, state='contradictory', fieldIssues=['copy-failures-exceed-unsuccessful-attempts'])
    if checked['unknownFieldCount'] or value.keys() != reported.keys() | {'failures'}:
        return dict(result, state='invalid-fields', fieldIssues=['unexpected-wrapper-fields'])
    for raw, projected in zip(value['failures'] or (), checked['failures']):
        if raw.keys() != projected['reported'].keys() | {'copyObservation'}:
            return dict(result, state='invalid-fields', fieldIssues=['unexpected-failure-fields'])
        observed, copy = raw['copyObservation'], projected['copyObservation']
        permitted = copy['reported'].keys() | {key for key, name in SNAPSHOT_WRITER_BOUNDARIES
            if name == raw['entry'] and copy.get(key, {}).get('state') == observed.get(key)}
        if observed.keys() != permitted:
            return dict(result, state='invalid-fields', fieldIssues=['unexpected-or-invalid-copy-fields'])
    try:
        size = len(json.dumps(value, ensure_ascii=False, separators=(',', ':')).encode('utf-8'))
    except UnicodeError:
        return dict(result, state='invalid-fields', fieldIssues=['invalid-utf8'])
    if size > 4096:
        return dict(result, state='byte-limit-exceeded', fieldIssues=['copy-failure-payload-exceeds-4096-bytes'])
    result['value'] = dict(coverage=state, reported=reported, failures=checked['failures'])
    return result


def prior_ordinary_export_terminal_evidence(availability,target):
    """Validate one same-process carry; it is never counted as a Native log row."""
    result=dict(state='not-observed',operation=None,terminal=None,bodyResultOk=None,
                exceptionClass=None,win32Error=None,retainedRowComparison='not-compared',
                originalLogRow=False,currentExportOutcomeInferred=False,uniqueFinalFileOrDurableCleanupProven=False)
    field='priorOrdinaryExportTerminal'
    if field not in availability:return result
    carry=availability[field]
    if not isinstance(carry,dict) or type(carry.get('schema')) is not int or carry['schema']!=1:
        return dict(result,state='unsupported-or-invalid-carrier')
    required_carrier={'schema','exportOperation','terminal'}
    if not required_carrier<=carry.keys()<=required_carrier|{
            'previousArchiveAttempt','finalOutputAfterMove','archiveAttemptSteps','snapshotCopyFailureEvidence','rendererReply'}:
        return dict(result,state='invalid-fields')
    operation=carry['exportOperation']
    if (not isinstance(operation,dict) or set(operation)!=ORDINARY_EXPORT_OPERATION_FIELDS or
            export_operation_record(carry)['state']!='reported-process-operation' or
            operation['writerPid']!=target['writerPid'] or
            operation['writerProcessCreation100ns']!=target['writerProcessCreation100ns'] or
            int(operation['ordinal'])+1!=int(target['ordinal']) or
            int(operation['startedTickMs'])>=int(target['startedTickMs'])):
        return dict(result,state='preceding-operation-unverified')
    terminal=carry['terminal']
    required={'ok','bodyReturned','attempts','exportElapsedMs','phaseTimingBuildFailures','terminalStage','exportMode'}
    optional={'cleanupOk','archiveElapsedMs','win32Error','reason','completion','exceptionClass','phaseElapsedMs','bodyResultOk'}
    if not isinstance(terminal,dict) or not required<=terminal.keys()<=required|optional:
        return dict(result,state='invalid-terminal')
    projection=ordinary_export_terminal_record(terminal)
    phase=projection['phaseTiming']
    reported=projection['reported']
    stage=reported.get('terminalStage')
    win32=terminal.get('win32Error')
    body_ok=terminal.get('bodyResultOk')
    exception=terminal.get('exceptionClass')
    archive_fields={'cleanupOk','archiveElapsedMs','win32Error','completion'}
    if (projection['fieldIssues'] or projection['terminalStageState']!='coherent-reported-stage' or
            reported.get('exportMode')!='full' or
            phase['state'] not in ('missing','reported-completed-phases') or phase['unrecognisedPhaseCount'] or
            (reported['bodyReturned'] and type(body_ok) is not bool) or
            (not reported['bodyReturned'] and 'bodyResultOk' in terminal) or
            (reported['bodyReturned'] and 'exceptionClass' in terminal) or
            (not reported['bodyReturned'] and exception not in ('std-exception','unknown-exception')) or
            (stage in ('archive-return','body-exception-after-archive') and
             (not archive_fields<=terminal.keys() or type(win32) is not int or not 0<=win32<1<<32)) or
            (stage in ('before-archive-return','body-exception-before-archive') and
             (archive_fields & terminal.keys() or reported['attempts']!=0 or reported['ok'] is not False or body_ok is True))):
        return dict(result,state='invalid-terminal')
    if 'previousArchiveAttempt' in carry:
        previous=previous_archive_attempt_record(carry)
        retry_fields={'schema','scope','attempt','reason','win32Error','temporaryRemoveError','stagingRemoveError','cleanupOk'}
        retry=carry['previousArchiveAttempt']
        if reported['attempts']!=2:
            previous.update(state='unexpected-attempt-count',reported={})
        elif isinstance(retry,dict) and retry.keys()!=retry_fields:
            previous.update(state='invalid-fields',reported={},fieldIssues=['unexpected-retry-fields'])
        elif previous['state']!='reported-previous-failed-attempt':
            previous['reported']={}
        result['previousArchiveAttempt']=previous
    if 'finalOutputAfterMove' in carry:
        result['finalOutputAfterMove'] = prior_final_output_after_move(
            carry['finalOutputAfterMove'], reported['attempts'], reported['ok'])
    if 'archiveAttemptSteps' in carry:
        packed_steps = carry['archiveAttemptSteps']
        result['archiveAttemptSteps'] = prior_archive_steps_record(
            packed_steps, reported['attempts'], reported.get('archiveElapsedMs'))
        if isinstance(packed_steps, dict) and 'childFailures' in packed_steps:
            result['archiveChildFailures'] = prior_archive_child_failures(
                packed_steps['childFailures'], result['archiveAttemptSteps'], reported['ok'])
        if isinstance(packed_steps, dict) and 'failedTemporaryLookups' in packed_steps:
            result['archiveTemporaryLookups'] = prior_archive_temporary_lookups(
                packed_steps['failedTemporaryLookups'], result['archiveAttemptSteps'], reported['ok'], reported.get('cleanupOk'))
    if 'snapshotCopyFailureEvidence' in carry:
        result['snapshotCopyFailureEvidence'] = prior_snapshot_copy_failure_record({
            'snapshotCopyFailureEvidence': carry['snapshotCopyFailureEvidence'],
            'attempts': reported['attempts'], 'ok': reported['ok']})
    if 'rendererReply' in carry:
        reply=carry['rendererReply']
        renderer=dict(state='invalid-fields',elapsedMs=None,replyOk=None,
                      scope='settings-request-to-reply;separate-from-native-export-duration;not-ui-paint-proof')
        if (isinstance(reply,dict) and reply.keys()=={'schema','replyOk','elapsedMs'} and
                type(reply['schema']) is int and reply['schema']==1 and type(reply['replyOk']) is bool and
                type(reply['elapsedMs']) is int and 0<=reply['elapsedMs']<(1<<53)):
            if reported['bodyReturned'] and reply['replyOk'] is body_ok:
                renderer.update(state='reported-matching-preceding-renderer-reply',
                    elapsedMs=reply['elapsedMs'],replyOk=reply['replyOk'])
            else:
                renderer['state']='preceding-return-mismatch'
        result['rendererReply']=renderer
    return dict(result,state='reported-in-memory-preceding-terminal',operation=dict(operation),terminal=projection,
                bodyResultOk=body_ok,exceptionClass=exception,win32Error=win32)


def ordinary_export_transaction_evidence(availability,selection,members):
    """Associate only one exact supported operation;never borrow nearby outcomes."""
    result=dict(scope='retained-selected-availability-to-reported-Native-terminal;diagnostic-correlation-only',
                state='receipt-unavailable',receiptSelectionStatus=selection.get('status'),
                archiveEntryOccurrence=selection.get('selectedOccurrence'),operation=dict(state='not-inspected'),
                windowsQuery=None,preSnapshotPhaseTiming=None,windowsSleepScheduling=None,terminal=None,
                precedingOperation=dict(state='current-operation-unavailable',candidateRows=None,terminal=None,
                    scope='immediately-preceding-ordinal;same-reported-writer-lifetime;historical-only',
                    currentExportOutcomeInferred=False,uniqueFinalFileOrDurableCleanupProven=False),
                precedingOperationCarry=None,
                cooldownOrigin=dict(state='current-operation-unavailable',originalExportId=None,
                    candidateRows=None,terminal=None,
                    scope='cooldown-origin-id-to-retained-native-terminal;same-reported-writer-lifetime;historical-only',
                    currentQueryExecutedInferred=False,currentExportOutcomeInferred=False),
                retainedTerminalRowsInspected=None,exactOperationRows=None,eligibleMatchingRows=None,
                sameIdDifferentOperationRows=None,writerUnknownExactOperationRows=None,writerConflictingExactOperationRows=None,
                matchingWitnesses=[],matchingWitnessLimit=16,omittedMatchingWitnesses=None,
                differentMatchingProjectionObserved=False,terminalRowsPaired=False,
                currentIpcOrContainingZipOutcomeProven=False,osWriterIdentityProven=False,
                uniqueFinalFileOrDurableCleanupProven=False,querySuccessFromZipOkInferred=False,
                queryInnerElapsedAddedToPhase=False,sleepOrDeviceSuccessProven=False)
    if selection.get('status')!='selected' or not isinstance(availability,dict):return result
    result['windowsQuery']=windows_export_query_record(availability)
    phase=availability.get('exportPhaseTiming')
    result['preSnapshotPhaseTiming']=export_phase_timing_record(phase if isinstance(phase,dict) else {},'elapsedMs')
    result['windowsSleepScheduling']=windows_sleep_scheduling_record(availability,result['preSnapshotPhaseTiming']['elapsedMs'])
    operation=export_operation_record(availability);result['operation']=operation
    if operation['state']=='reported-pid-operation;birth-unknown':result['state']='operation-birth-unknown';return result
    if operation['state']!='reported-process-operation':result['state']='operation-unusable';return result
    target=availability['exportOperation']
    if set(target)!=ORDINARY_EXPORT_OPERATION_FIELDS:result['state']='operation-unsupported-fields';return result
    target=dict(target)
    carry=prior_ordinary_export_terminal_evidence(availability,target)
    result['precedingOperationCarry']=carry
    cooldown=result['cooldownOrigin']
    origin=cooldown_export_origin_reference(result['windowsQuery'],target,cooldown)
    originTerminal=None
    preceding=result['precedingOperation']
    priorOrdinal=int(target['ordinal'])-1
    preceding.update(state='terminal-not-observed' if priorOrdinal else 'no-preceding-ordinal',candidateRows=0)
    priorTerminal=None
    result.update(state='terminal-not-observed',retainedTerminalRowsInspected=0,exactOperationRows=0,eligibleMatchingRows=0,
                  sameIdDifferentOperationRows=0,writerUnknownExactOperationRows=0,writerConflictingExactOperationRows=0)
    head=[];tail=collections.deque(maxlen=8);firstProjection=None;single=None
    for filename,rows,metadata in members:
        if not isinstance(filename,str) or len(filename)>128 or not re.fullmatch(r'native-(?:lifecycle|detail)\.log(?:\.(?:prev|previous|\d+))?',filename):continue
        for line,row in rows:
            if not isinstance(row,dict) or row.get('event')!='log-export-result':continue
            result['retainedTerminalRowsInspected']+=1
            candidate=row.get('exportOperation')
            if not isinstance(candidate,dict):continue
            if origin is not None and candidate.get('id')==origin['id']:
                cooldown['candidateRows']+=1
                if cooldown['candidateRows']>1:
                    cooldown['state']='ambiguous-origin-terminals'
                    originTerminal=None
                elif (candidate!=origin or export_operation_record(row)['state']!='reported-process-operation' or
                        type(row.get('pid')) is not int or row['pid']!=origin['writerPid'] or
                        row.get('writerProcessCreation100ns')!=origin['writerProcessCreation100ns']):
                    cooldown['state']='origin-terminal-writer-or-operation-conflict'
                else:
                    cooldown['state']='unique-reported-origin-terminal'
                    occurrence=metadata.get('archiveEntryOccurrence') if isinstance(metadata,dict) else None
                    originTerminal=(filename,line,occurrence,row)
            if (priorOrdinal and candidate.get('ordinal')==str(priorOrdinal) and
                    type(candidate.get('writerPid')) is int and candidate['writerPid']==target['writerPid'] and
                    candidate.get('writerProcessCreation100ns')==target['writerProcessCreation100ns']):
                preceding['candidateRows']+=1
                if preceding['candidateRows']>1:
                    preceding['state']='ambiguous-retained-terminals'
                    priorTerminal=None
                elif (set(candidate)!=ORDINARY_EXPORT_OPERATION_FIELDS or
                        export_operation_record(row)['state']!='reported-process-operation' or
                        int(candidate['startedTickMs'])>=int(target['startedTickMs']) or
                        type(row.get('pid')) is not int or row['pid']!=target['writerPid'] or
                        row.get('writerProcessCreation100ns')!=target['writerProcessCreation100ns']):
                    preceding['state']='conflicting-or-incomplete-terminal'
                else:
                    preceding['state']='unique-reported-preceding-terminal'
                    occurrence=metadata.get('archiveEntryOccurrence') if isinstance(metadata,dict) else None
                    priorTerminal=(filename,line,occurrence,row,candidate)
            if candidate!=target or export_operation_record(row)['state']!='reported-process-operation':
                if candidate.get('id')==target['id']:result['sameIdDifferentOperationRows']+=1
                continue
            result['exactOperationRows']+=1
            # Complete raw-object equality follows canonical typed target validation.
            # Outer Native identity must independently report the same tuple.
            pidValue=row.get('pid');birthValue=row.get('writerProcessCreation100ns')
            pidKnown=type(pidValue) is int and 0<pidValue<(1<<32)
            birthKnown=(isinstance(birthValue,str) and 0<len(birthValue)<=20 and birthValue.isascii() and birthValue.isdecimal() and
                        str(int(birthValue))==birthValue and 0<int(birthValue)<(1<<64))
            if not pidKnown or not birthKnown:
                writer='not-observed-or-invalid';result['writerUnknownExactOperationRows']+=1
            elif pidValue!=target['writerPid'] or birthValue!=target['writerProcessCreation100ns']:
                writer='conflicting';result['writerConflictingExactOperationRows']+=1
            else:writer='reported-same-row-values-match';result['eligibleMatchingRows']+=1
            projection=ordinary_export_terminal_record(row)
            if firstProjection is None:firstProjection=projection
            elif projection!=firstProjection:result['differentMatchingProjectionObserved']=True
            occurrence=metadata.get('archiveEntryOccurrence') if isinstance(metadata,dict) else None
            witness=dict(file=filename,line=line if type(line) is int and line>0 else None,
                         archiveEntryOccurrence=occurrence if type(occurrence) is int and occurrence>=0 else None,
                         sameRowWriterState=writer,terminal=projection)
            if len(head)<8:head.append(witness)
            else:tail.append(witness)
            if result['exactOperationRows']==1 and writer=='reported-same-row-values-match':
                single=dict(witness,archiveTiming=archive_attempt_timing_record(row))
    if priorTerminal is not None:
        filename,line,occurrence,row,candidate=priorTerminal
        preceding['terminal']=dict(file=filename,line=line if type(line) is int and line>0 else None,
            archiveEntryOccurrence=occurrence if type(occurrence) is int and occurrence>=0 else None,
            operation=dict(candidate),startTickDeltaMs=int(target['startedTickMs'])-int(candidate['startedTickMs']),
            terminal=ordinary_export_terminal_record(row),archiveTiming=archive_attempt_timing_record(row))
    if carry['state']=='reported-in-memory-preceding-terminal':
        final_output = carry.get('finalOutputAfterMove')
        archive_steps = carry.get('archiveAttemptSteps')
        archive_failures = carry.get('archiveChildFailures')
        temporary_lookups = carry.get('archiveTemporaryLookups')
        copy_failure = carry.get('snapshotCopyFailureEvidence')
        if copy_failure is not None and copy_failure['state'] in PRIOR_COPY_FAILURE_STATES:
            copy_failure['retainedRowComparison'] = preceding['state']
            if priorTerminal is not None:
                raw = priorTerminal[3]
                original = prior_snapshot_copy_failure_record(raw)
                compact = carry['terminal']['reported']
                copy_failure['retainedRowComparison'] = (
                    'reported-fields-conflict' if priorTerminal[4] != carry['operation'] or any(
                        type(raw.get(k)) is not type(compact[k]) or raw[k] != compact[k] for k in ('attempts', 'ok')) else
                    'raw-copy-failure-not-observed' if original['state'] in ('missing', 'null') else
                    'reported-fields-conflict' if original['state'] == 'reported-no-observed-mandatory-copy-failures' else
                    'raw-copy-failure-unusable' if original['state'] not in PRIOR_COPY_FAILURE_STATES else
                    'reported-fields-agree' if original['value'] == copy_failure['value'] else 'reported-fields-conflict')
        if archive_steps is not None and archive_steps['state'] == 'reported-completed-steps':
            archive_steps['retainedRowComparison'] = preceding['state']
            if priorTerminal is not None:
                raw = priorTerminal[3]
                count = len(archive_steps['attempts'])
                archive_ms = carry['terminal']['reported']['archiveElapsedMs']
                archive_steps['retainedRowComparison'] = (compare_prior_archive_steps(
                    archive_steps['attempts'], preceding['terminal']['archiveTiming'])
                    if (priorTerminal[4] == carry['operation'] and type(raw.get('attempts')) is int and
                        raw['attempts'] == count and type(raw.get('archiveElapsedMs')) is int and
                        raw['archiveElapsedMs'] == archive_ms) else 'reported-fields-conflict')
        if archive_failures is not None and archive_failures['state'] == 'reported-archive-child-failures':
            archive_failures['retainedRowComparison'] = preceding['state']
            if priorTerminal is not None:
                raw = priorTerminal[3]
                compact = carry['terminal']['reported']
                archive_failures['retainedRowComparison'] = (compare_prior_archive_child_failures(
                    archive_failures['failures'], preceding['terminal']['archiveTiming'])
                    if priorTerminal[4] == carry['operation'] and all(
                        type(raw.get(key)) is type(compact[key]) and raw[key] == compact[key]
                        for key in ('attempts', 'ok')) else 'reported-fields-conflict')
        if temporary_lookups is not None and temporary_lookups['state'] == 'reported-archive-temporary-lookups':
            temporary_lookups['retainedRowComparison'] = preceding['state']
            if priorTerminal is not None:
                raw = priorTerminal[3]
                compact = carry['terminal']['reported']
                temporary_lookups['retainedRowComparison'] = (compare_prior_archive_temporary_lookups(
                    temporary_lookups['lookups'], preceding['terminal']['archiveTiming'])
                    if priorTerminal[4] == carry['operation'] and all(
                        type(raw.get(k)) is type(compact[k]) and raw[k] == compact[k]
                        for k in ('attempts', 'ok', 'cleanupOk')) else 'reported-fields-conflict')
        if priorTerminal is None:
            carry['retainedRowComparison']=preceding['state']
            if final_output is not None and final_output['state']=='reported-final-check-after-move':
                final_output['retainedRowComparison'] = preceding['state']
        else:
            compact=availability['priorOrdinaryExportTerminal']['terminal']
            raw=priorTerminal[3]
            agrees=(priorTerminal[4]==carry['operation'] and all(
                (isinstance(raw.get(key),dict) and all(type(raw[key].get(k)) is int and raw[key][k]==v
                                                       for k,v in value.items())) if key=='phaseElapsedMs' else
                (type(raw.get(key)) is type(value) and raw[key]==value)
                for key,value in compact.items() if key!='bodyResultOk'))
            carry['retainedRowComparison']='reported-fields-agree' if agrees else 'reported-fields-conflict'
            retry=carry.get('previousArchiveAttempt')
            if retry is not None and retry['state']=='reported-previous-failed-attempt':
                original_retry=previous_archive_attempt_record(raw)
                retry['retainedRowComparison']=(
                    'raw-retry-not-observed' if original_retry['state'] in ('missing','null') else
                    'raw-retry-unusable' if original_retry['state']!='reported-previous-failed-attempt' else
                    'reported-fields-agree' if original_retry==retry else 'reported-fields-conflict')
            if final_output is not None and final_output['state']=='reported-final-check-after-move':
                output = final_output['reported']
                final_output['retainedRowComparison'] = (compare_prior_final_output_after_move(
                    output, preceding['terminal']['archiveTiming'], output['attempt'], raw.get('ok'))
                    if (priorTerminal[4]==carry['operation'] and type(raw.get('ok')) is bool and raw['ok']==compact['ok']
                        and type(raw.get('attempts')) is int and raw['attempts']==output['attempt'])
                    else 'reported-fields-conflict')
    if originTerminal is not None:
        filename,line,occurrence,row=originTerminal
        cooldown['terminal']=dict(file=filename,line=line if type(line) is int and line>0 else None,
            archiveEntryOccurrence=occurrence if type(occurrence) is int and occurrence>=0 else None,
            operation=origin,terminal=ordinary_export_terminal_record(row))
    result['matchingWitnesses']=head+list(tail)
    result['omittedMatchingWitnesses']=max(0,result['exactOperationRows']-len(result['matchingWitnesses']))
    if result['exactOperationRows']>1:result['state']='multiple-matching-operation-rows'
    elif result['eligibleMatchingRows']==1:
        result.update(state='unique-reported-terminal-row',terminal=single,terminalRowsPaired=True)
    elif result['writerConflictingExactOperationRows']:result['state']='matching-operation-writer-conflict'
    elif result['writerUnknownExactOperationRows']:result['state']='matching-operation-writer-unavailable'
    return result


def staged_root_inspection_window(row):
    size, earlier = (row.get(k) for k in ('bytes', 'earlierBytesNotInspected'))
    if type(size) is not int or type(earlier) is not int or not 0 <= earlier <= size < (1 << 64):
        return None
    fmt, scope, status = (row.get(k) for k in ('format', 'inspectionScope', 'status'))
    window = size - earlier
    if fmt == 'json-lines' and status in ('inspected', 'snapshot-partial-tail-retained') and (
            (scope == 'retained-file-only' and earlier == 0 and size <= 8 * 1024 * 1024) or
            (scope == 'retained-tail-only' and earlier > 0 and window == 8 * 1024 * 1024 and
             row.get('leadingBoundaryLineNotInspected') is True)):
        return fmt, scope, window, earlier
    if fmt == 'opaque-file' and status == 'inspected' and (
            (scope == 'final-byte-only' and size > 0 and window == 1) or
            (scope == 'empty-file' and size == 0)):
        return fmt, scope, window, earlier
    return None


def staged_root_inspection_footprint(snapshot, work):
    """Aggregate logical inspection scopes, without attributing time or physical IO."""
    call = work['calls'].get('rootInspection')
    if work['state'] != 'reported-snapshot-work' or not call or call['state'] != 'reported-completed-calls':
        return None
    reported = call['reported']
    result = dict(state='files-unavailable', reportedReturnedCalls=reported['returnedCalls'], jsonLines=None, opaque=None,
                  scope='selected-root-inspection-windows;not-parsed-bytes-physical-io-or-per-format-time')
    files = snapshot.get('files')
    if not isinstance(files, list):
        return result
    if len(files) > 4096:
        return dict(result, state='file-row-budget-exceeded')
    indexes = set()
    json_lines = dict(files=0, completeWindows=0, tailWindows=0, windowBytes=0, earlierBytesNotInspected=0)
    opaque = dict(files=0, finalByteOnly=0, emptyFiles=0)
    for row in files:
        if not isinstance(row, dict) or row.get('evidenceScope') != 'actual-staged-bytes-not-live-source':
            continue
        ordinal, entry = (row.get(k) for k in ('listIndex', 'entry'))
        window = staged_root_inspection_window(row)
        if (row.get('originalBytesRetained') is not True or type(ordinal) is not int or not 0 <= ordinal < (1 << 64) or
                not isinstance(entry, str) or not 0 < len(entry) <= 2048 or entry in ('.', '..') or
                any(c in '/\\:' or ord(c) < 32 for c in entry) or window is None):
            return dict(result, state='incomplete-inspection-rows')
        if ordinal in indexes:
            return dict(result, state='duplicate-list-index')
        indexes.add(ordinal)
        fmt, scope, window_bytes, earlier = window
        if fmt == 'json-lines':
            json_lines['files'] += 1
            json_lines['tailWindows' if earlier else 'completeWindows'] += 1
            json_lines['windowBytes'] += window_bytes
            json_lines['earlierBytesNotInspected'] += earlier
        else:
            opaque['files'] += 1
            opaque['finalByteOnly' if scope == 'final-byte-only' else 'emptyFiles'] += 1
    if reported['exceptionalCalls'] or len(indexes) != reported['returnedCalls']:
        return dict(result, state='inspection-count-mismatch')
    return dict(result, state='matched-inspection-rows', jsonLines=json_lines, opaque=opaque)


def staged_root_inspection_maximum(snapshot, work):
    """Resolve the root-list ordinal only against this checkpoint's own files."""
    call = work['calls'].get('rootInspection')
    if call is None or call['state'] != 'reported-completed-calls':
        return None
    return staged_root_inspection_row(snapshot, call['reported']['maxSourceOrdinal'], call['reported']['maxElapsedMs'])


def staged_root_inspection_row(snapshot, ordinal, elapsed_ms):
    result = dict(state='files-unavailable', listIndex=ordinal, elapsedMs=elapsed_ms,
                  fileRowIndex=None, entry=None, stagedBytes=None)
    files = snapshot.get('files')
    if not isinstance(files, list):
        return result
    result['state'] = 'ordinal-not-observed'
    match = None
    for index, row in enumerate(files):
        if not isinstance(row, dict) or type(row.get('listIndex')) is not int or row['listIndex'] != ordinal:
            continue
        if match is not None:
            return {**result, 'state': 'duplicate-list-index'}
        match = index, row
    if match is None:
        return result
    index, row = match
    entry = row.get('entry')
    if (row.get('evidenceScope') != 'actual-staged-bytes-not-live-source' or
            row.get('originalBytesRetained') is not True or not isinstance(entry, str) or
            not 0 < len(entry) <= 2048 or entry in ('.', '..') or
            any(c in '/\\:' or ord(c) < 32 for c in entry)):
        return {**result, 'state': 'unrecognized-root-file-row'}
    size = row.get('bytes')
    result.update(state='matched-staged-file-row', fileRowIndex=index, entry=entry,
                  stagedBytes=size if type(size) is int and 0 <= size < (1 << 64) else None)
    window = staged_root_inspection_window(row)
    if window is not None:
        fmt, scope, window_bytes, earlier = window
        witness = dict(format=fmt, inspectionScope=scope, windowBytes=window_bytes, earlierBytesNotInspected=earlier,
                       scope='logical-staged-window;not-parsed-bytes-physical-io-or-cpu-time')
        if fmt == 'json-lines':
            fields = ('validJsonRows', 'invalidJsonRows', 'oversizeRowsNotParsed')
            counts = {key: row.get(key) for key in fields}
            if (all(type(value) is int and 0 <= value <= window_bytes for value in counts.values()) and
                    sum(counts.values()) <= window_bytes):
                witness['reportedWindowRowCounts'] = counts
        result['inspectionWindow'] = witness
    return result


def staged_root_inspection_leaders(snapshot, work):
    rank = work.get('rootInspectionLeaders', dict(state='missing', entries=[], limit=None))
    files = snapshot.get('files')
    if isinstance(files, list) and len(files) > 4096:
        return dict(rank, fileResolution='file-row-budget-exceeded')
    return dict(rank, entries=[staged_root_inspection_row(snapshot, row['sourceOrdinal'], row['elapsedMs'])
                              for row in rank['entries']])


def retry_checkpoint_first_attempt(first, previous):
    result = dict(state='reported-first-attempt-before-retry', totalElapsedMs=None, completedStepElapsedMs=None,
                  failedCleanupState='not-inspected', failedCleanup=None, temporaryAfterFailedCleanup=None)
    if first['unrecognizedPhaseCount'] or first['promotion']['promotion'].get('accepted') is True:
        return dict(result, state='first-attempt-conflicts-with-retry-checkpoint')
    result.update(totalElapsedMs=first['reported']['totalElapsedMs'],
                  completedStepElapsedMs=first['completedStepElapsedMs'])
    cleanup = first['cleanup']
    result['failedCleanupState'] = cleanup['state']
    if cleanup['state'] != 'reported-cleanup-call-observations':
        return result
    sites = {entry['site']: entry for entry in cleanup['sites']}
    temporary, staging = sites['failed-temporary'], sites['failed-staging']
    if (sites['promoted-staging']['state'] != 'not-entered' or
            temporary['state'] != 'reported-error-code' or staging['state'] != 'reported-error-code' or
            (temporary['errorValue'] & 0xffffffff) != previous['temporaryRemoveError'] or
            (staging['errorValue'] & 0xffffffff) != previous['stagingRemoveError']):
        result['failedCleanupState'] = 'cleanup-conflicts-with-retry-summary'
        return result
    result.update(failedCleanupState='reported-and-matches-retry-summary', failedCleanup=[temporary, staging],
                  temporaryAfterFailedCleanup=first.get('failedTemporaryAfterCleanup'))
    return result


def ordinary_export_snapshot_boundary(transaction, snapshot, source):
    """Bind a staged checkpoint without treating it as the archive's result."""
    terminal_state = transaction['state']
    result = dict(scope='selected-staged-checkpoint;before-current-attempt-tar-promotion-cleanup',
                  state='snapshot-receipt-unavailable', receiptSelectionStatus=source['selectionStatus'],
                  archiveEntryOccurrence=source['selectedOccurrence'], snapshotOperationMatched=False,
                  attempt=None, timingState='not-inspected', completedStepElapsedMs=None,
                  snapshotWork=None, rootInspectionMaximum=None, rootInspectionFootprint=None, rootInspectionLeaders=None,
                  previousAttemptState='not-inspected', previousAttempt=None, nativeTerminalState=terminal_state,
                  terminalAttemptCountBelowCheckpoint=None, containingZipOutcomeProven=False)
    if source['selectionStatus'] != 'selected' or not isinstance(snapshot, dict):
        return result
    target = transaction['operation']
    if target['state'] != 'reported-process-operation' or terminal_state == 'operation-unsupported-fields':
        result['state'] = 'availability-operation-unavailable'
        return result
    context = source['archiveAttemptContext']
    if context['state'] != 'reported-staging-attempt':
        result['state'] = 'snapshot-context-' + context['state']
        return result
    operation = snapshot['archiveAttemptContext']['exportOperation']
    if context['unprojectedFieldCount'] or set(operation) != ORDINARY_EXPORT_OPERATION_FIELDS:
        result['state'] = 'snapshot-context-unsupported-fields'
        return result
    if context['exportOperation']['state'] != 'reported-process-operation':
        result['state'] = 'snapshot-operation-birth-unknown'
        return result
    if operation != target['reported']:
        result['state'] = 'snapshot-operation-conflict'
        return result

    attempt = context['reported']['attempt']
    result.update(state='matched-staging-attempt', snapshotOperationMatched=True, attempt=attempt)
    previous = source['previousArchiveAttempt']
    result['previousAttemptState'] = previous['state']
    if previous['state'] == 'reported-previous-failed-attempt':
        if attempt == 2:
            result['previousAttempt'] = previous['reported']
        else:
            result['previousAttemptState'] = 'previous-attempt-conflicts-with-context'
    timing = source['archiveAttemptTiming']
    result['timingState'] = timing['state']
    if timing['state'] == 'reported-completed-steps':
        rows = timing['attempts']
        if len(rows) != attempt or not all(row['reported']['attemptBodyReturned'] for row in rows[:-1]):
            result['timingState'] = 'timing-conflicts-with-attempt-context'
        else:
            current = rows[-1]
            phases = current['completedStepElapsedMs']
            if (current['reported']['attemptBodyReturned'] is False and not current['unrecognizedPhaseCount'] and
                    set(phases) == {'stagingPreparation', 'sourceSnapshot'}):
                work = current['snapshotWork']
                result.update(timingState='reported-pre-tar-checkpoint', completedStepElapsedMs=phases,
                              snapshotWork={'state': work['state'], 'calls': work['calls'],
                                            'rootInspectionBatch': work.get('rootInspectionBatch')},
                              rootInspectionMaximum=staged_root_inspection_maximum(snapshot, work),
                              rootInspectionFootprint=staged_root_inspection_footprint(snapshot, work),
                              rootInspectionLeaders=staged_root_inspection_leaders(snapshot, work))
                if result['previousAttempt'] is not None:
                    result['previousAttemptDetail'] = retry_checkpoint_first_attempt(rows[0], result['previousAttempt'])
            else:
                result['timingState'] = 'timing-conflicts-with-snapshot-boundary'

    if terminal_state == 'unique-reported-terminal-row':
        attempts = transaction['terminal']['terminal']['reported'].get('attempts')
        if attempts is not None:
            result['terminalAttemptCountBelowCheckpoint'] = attempts < attempt
    return result


def export_checkpoint_wall_lower_bound(availability, transaction, boundary):
    """Add only disjoint, completed intervals from the exact staged operation."""
    if boundary['timingState'] != 'reported-pre-tar-checkpoint':
        return None
    result = dict(scope='completed-reported-wall-ms-through-pre-tar-checkpoint;not-end-to-end-export-time',
                  state='pre-snapshot-timing-unavailable', attempt=boundary['attempt'], lowerBoundMs=None)
    phase = availability.get('exportPhaseTiming')
    if not isinstance(phase, dict) or 'completedTickMs' not in phase:
        return result
    result['state'] = 'pre-snapshot-timing-unsupported-or-incomplete'
    failures, tick = phase.get('evidenceBuildFailures'), phase['completedTickMs']
    if (phase.get('scope') != 'completed-pre-snapshot-phases;current-export-archive-and-terminal-not-yet-observed' or
            type(failures) is not int or failures != 0 or
            not isinstance(tick, str) or not 0 < len(tick) <= 20 or not tick.isascii() or not tick.isdecimal()):
        return result
    finished = int(tick)
    if str(finished) != tick or finished >= 1 << 64:
        return result
    phases = transaction['preSnapshotPhaseTiming']
    elapsed = phases['elapsedMs']
    common = {'flush', 'steamDiscovery', 'cachedStateAndFileDiscovery'}
    layouts = (common | {'windowsSystemPowerReport', 'remainingStateAndManifest'},
               common | {'windowsReportAndRemainingState', 'manifestWrite'})
    if (phases['state'] != 'reported-completed-phases' or phases['unrecognisedPhaseCount'] or
            elapsed.keys() not in layouts):
        return result
    started = int(transaction['operation']['reported']['startedTickMs'])
    if finished < started or finished - started != sum(elapsed.values()):
        return dict(result, state='pre-snapshot-ticks-conflict', completedTickMs=tick,
                    tickDeltaMs=finished - started, reportedPhaseSumMs=sum(elapsed.values()))

    # Attempt 1's total already contains its cleanup and any retry delay.
    previous = boundary.get('previousAttemptDetail') or {}
    first = None
    if (boundary['attempt'] == 2 and previous.get('state') == 'reported-first-attempt-before-retry' and
            previous['failedCleanupState'] != 'cleanup-conflicts-with-retry-summary'):
        first = previous['totalElapsedMs']
    steps = boundary['completedStepElapsedMs']
    archive_minimum = sum(steps.values()) + (first if first is not None else 0)
    if finished + archive_minimum >= 1 << 64:
        return dict(result, state='checkpoint-ticks-out-of-range', completedTickMs=tick,
                    preSnapshotMs=finished - started, archiveThroughCheckpointMs=archive_minimum)
    lower_bound = finished - started + archive_minimum
    result.update(state='reported-lower-bound', completedTickMs=tick, preSnapshotMs=finished - started,
                checkpointStepsMs=steps, firstAttemptTotalIncludingRetryDelayMs=first,
                archiveThroughCheckpointMs=archive_minimum, lowerBoundMs=lower_bound)
    scheduling = transaction['windowsSleepScheduling']
    if ('windowsReportAndRemainingState' in elapsed and scheduling['state'] == 'reported-overlap-intervals' and
            scheduling['reported']['mode'] == 'overlapped-task'):
        join = scheduling['reported']['joinMs']
        result['windowsReportJoin'] = dict(elapsedMs=join, otherCompletedWallMs=lower_bound - join,
            scope='already-in-checkpoint-wall-and-combined-phase;not-query-cpu-or-measured-speedup')
    return result


def export_checkpoint_wall_terminal_comparison(bound, pre_snapshot_phases, view, snapshot_comparison):
    if not bound or bound['state'] != 'reported-lower-bound':
        return None
    sources, conflicts = {}, list((snapshot_comparison or {}).get('conflicts', ()))

    def compare(source, terminal, attempts):
        if terminal is None:
            return
        reported = terminal['reported']
        total, archive = reported.get('exportElapsedMs'), reported.get('archiveElapsedMs')
        if total is not None or archive is not None:
            sources[source] = dict(exportElapsedMs=total, archiveElapsedMs=archive)
        if total is not None and total < bound['lowerBoundMs']:
            conflicts.append(source + '-export-shorter-than-bound')
        if archive is not None and archive < bound['archiveThroughCheckpointMs']:
            conflicts.append(source + '-archive-shorter-than-bound')
        phases = terminal['phaseTiming']['elapsedMs']
        if any(phases[k] != pre_snapshot_phases[k] for k in phases.keys() & pre_snapshot_phases.keys()):
            conflicts.append(source + '-vs-pre-snapshot-phases')
        if reported.get('attempts', bound['attempt']) < bound['attempt']:
            reason = source + '-attempt-count'
            if reason not in conflicts:
                conflicts.append(reason)
        first = bound['firstAttemptTotalIncludingRetryDelayMs']
        if first is not None and attempts:
            row = attempts[0].get('reported', attempts[0])
            if row.get('attempt') == 1 and row.get('totalElapsedMs') is not None and row['totalElapsedMs'] != first:
                conflicts.append(source + '-vs-first-attempt-total')

    timing = view['archiveTiming'] or {}
    compare('native-terminal', view['terminal'],
            timing.get('attempts') if timing.get('state') == 'reported-completed-steps' else None)
    carry = view.get('precedingCarryFromOtherArchive') or {}
    core = carry.get('core')
    if core is not None:
        steps = carry.get('details', {}).get('archiveAttemptSteps') or {}
        compare('preceding-terminal-carry', core['terminal'], steps.get('value'))
    return dict(state='conflicting-report' if conflicts else 'reported-timings-compatible' if sources else 'no-comparable-terminal',
                checkpointLowerBoundMs=bound['lowerBoundMs'], terminalElapsedMsBySource=sources, conflicts=conflicts)


def cross_archive_terminal_details(row):
    """Keep stage timing, snapshot call work and file checks independently comparable."""
    timing = archive_attempt_timing_record(row)
    attempts = []
    checks = []
    snapshot_work = []
    if timing['state'] == 'reported-completed-steps':
        for attempt in timing['attempts']:
            index = attempt['indexWork']
            attempts.append({'reported': attempt['reported'], 'completedStepElapsedMs': attempt['completedStepElapsedMs'],
                             'indexWork': {'state': index['state'], 'reported': index['reported'],
                                           'calls': index['calls']}})
            checks.append({name: attempt[name] for name in ('outputCheck', 'promotion', 'cleanup', 'cleanupReturn')})
            if 'failedTemporaryAfterCleanup' in attempt:
                checks[-1]['failedTemporaryAfterCleanup'] = attempt['failedTemporaryAfterCleanup']
            checks[-1]['attempt'] = attempt['reported']['attempt']
            work = attempt['snapshotWork']
            snapshot_work.append({'attempt': attempt['reported']['attempt'], 'state': work['state'],
                                  'calls': work['calls'], 'rootInspectionLeaders': work.get('rootInspectionLeaders'),
                                  'rootInspectionBatch': work.get('rootInspectionBatch')})
    summarized_timing = {'state': timing['state'], 'attempts': attempts}
    if 'wallBreakdown' in timing:
        summarized_timing['wallBreakdown'] = timing['wallBreakdown']
    file_checks = {'timingRecordState': timing['state'], 'attempts': checks,
                   'previousAttempt': previous_archive_attempt_record(row)}
    final_state, final = reported_final_output_after_move(timing, row.get('attempts'), row.get('ok'))
    if final is not None:
        file_checks['reportedFinalOutputAfterMove'] = final
    elif final_state == 'reported-fields-conflict':
        file_checks['reportedFinalOutputRejected'] = True
    return (summarized_timing, file_checks,
            {'timingRecordState': timing['state'], 'attempts': snapshot_work,
             'scope': 'reported-call-spans-within-sourceSnapshot;do-not-add-to-phase-or-assign-file-name'})


def export_snapshot_timing_comparison(boundary, view):
    """Reconcile only the two completed steps captured before tar for this attempt."""
    if boundary['timingState'] != 'reported-pre-tar-checkpoint':
        return None
    attempt = boundary['attempt']
    checkpoint = boundary['completedStepElapsedMs']
    sources, conflicts = {}, []

    def compare(source, rows):
        for row in rows:
            reported = row.get('reported', row)
            if reported['attempt'] != attempt or not reported.get('attemptBodyReturned', True):
                continue
            steps = row['completedStepElapsedMs']
            if checkpoint.keys() <= steps.keys():
                values = {name: steps[name] for name in checkpoint}
                sources[source] = values
                if values != checkpoint:
                    conflicts.append(source + '-vs-checkpoint')

    timing = view['archiveTiming'] or {}
    terminal = (view['terminal'] or {}).get('reported', {})
    if view['state'] == 'ambiguous-or-conflicting-terminal' or view.get('differentArchiveTimingProjections'):
        conflicts.append('native-terminal-evidence')
    elif timing.get('state') == 'reported-completed-steps' and terminal.get('attempts') == len(timing['attempts']):
        compare('native-terminal', timing['attempts'])
    if terminal.get('attempts', attempt) < attempt:
        conflicts.append('native-terminal-attempt-count')

    carry = view.get('precedingCarryFromOtherArchive') or {}
    detail = carry.get('details', {}).get('archiveAttemptSteps') or {}
    comparison = carry.get('nativeTerminalProjectionComparison')
    if detail.get('conflicting') or comparison in ('conflicting-carried-terminals', 'reported-core-fields-conflict'):
        conflicts.append('preceding-terminal-carry-evidence')
    elif comparison in ('reported-core-fields-agree', 'terminal-not-observed') and detail.get('value') is not None:
        compare('preceding-terminal-carry', detail['value'])
        if len(detail['value']) < attempt:
            conflicts.append('preceding-terminal-carry-attempt-count')

    return dict(scope='same-operation-and-attempt;completed-pre-tar-wall-ms-only', attempt=attempt,
                state='conflicting-report' if conflicts else 'reported-timings-agree' if sources else 'no-comparable-terminal',
                checkpointElapsedMs=checkpoint, terminalElapsedMsBySource=sources, conflicts=conflicts)


def export_output_size_comparison(view, supplied_bytes):
    """Compare length only, using the receipt's operation association already made by the reader."""
    if type(supplied_bytes) is not int or not 0 < supplied_bytes < 1 << 64:
        return None
    original = (view['archiveChecks'] or {}).get('reportedFinalOutputAfterMove')
    carry = view.get('precedingCarryFromOtherArchive') or {}
    detail = carry.get('details', {}).get('finalOutputAfterMove') or {}
    comparison = carry.get('nativeTerminalProjectionComparison')
    carried = detail.get('value')
    if not (original or carried or detail.get('conflicting') or view.get('rawFinalOutputReported')):
        return None
    result = dict(scope='supplied-zip-length-vs-reported-post-move-size;exact-reported-operation',
                  suppliedZipBytes=supplied_bytes, reportedFinalSizeBytes=None, sources=[],
                  sameFileOrContentProven=False, desktopZipCountProven=False)
    if (view['state'] == 'ambiguous-or-conflicting-terminal' or view.get('differentArchiveCheckProjections') or
            detail.get('conflicting') or comparison in ('conflicting-carried-terminals', 'reported-core-fields-conflict') or
            (carried is not None and (view['archiveChecks'] or {}).get('reportedFinalOutputRejected')) or
            (original is not None and carried is not None and original != carried)):
        return dict(result, state='conflicting-report')
    if comparison not in ('reported-core-fields-agree', 'terminal-not-observed'):
        carried = None
    check = original or carried
    if not check or type(check['sizeBytes']) is not int:
        return None
    result['sources'] = [name for value, name in ((original, 'native-terminal'), (carried, 'preceding-terminal-carry')) if value]
    size = check['sizeBytes']
    return dict(result, state='same-size' if size == supplied_bytes else 'different-size',
                reportedFinalSizeBytes=size, attempt=check['attempt'], reportedFinalCheckState=check['state'])


def legacy_public_archive_pairs(archives, *, max_groups=4096, max_role_references=8):
    """Classify supplied legacy names only, using exact selected receipt bytes."""
    groups = {}
    groups_limited = 0
    for archive in archives:
        local = archive['commonEvidence']['ordinaryExportTransaction']
        operation = local['operation']
        if operation['state'] != 'reported-process-operation' or local['state'] == 'operation-unsupported-fields':
            continue
        operation = operation['reported']
        name = Path(archive['zipPath']).name
        final_pattern = r'YeManCC-logs-[0-9]{8}-[0-9]{6}-' + re.escape(operation['id']) + r'\.zip'
        match = re.fullmatch(f'({final_pattern})(?:\\.attempt-{operation["writerPid"]}-([0-9]{{1,20}})-([0-9]{{1,20}})-([12])\\.zip)?', name)
        if match is None:
            continue
        final_name, tick, sequence, attempt = match.groups()
        if attempt is not None and (str(int(tick)) != tick or str(int(sequence)) != sequence or
                not int(operation['startedTickMs']) <= int(tick) < (1 << 64) or int(sequence) >= (1 << 64)):
            continue
        doc, = [doc for doc in archive['metadataSelectionEvidence']['documents']
                if doc['file'] == 'log-export-availability.json']
        refs = doc['references']
        if (doc['status'] != 'selected' or doc['archiveEntries'] != 1 or doc['referenceCount'] != 1 or len(refs) != 1 or
                refs[0]['readOutcome'] != 'read' or refs[0]['archiveEntryOccurrence'] != doc['selectedOccurrence']):
            continue
        receipt_hash = refs[0]['sha256']
        key = tuple(operation[k] for k in sorted(ORDINARY_EXPORT_OPERATION_FIELDS)) + (final_name, receipt_hash)
        if key not in groups and len(groups) >= max_groups:
            groups_limited += 1
            continue
        group = groups.setdefault(key, dict(operation=operation, finalBasename=final_name,
            availabilityMemberSha256=receipt_hash, roles={'final-name': {}, 'legacy-attempt-name': {}}, referencesOmitted=0))
        role = 'final-name' if attempt is None else 'legacy-attempt-name'
        role_refs = group['roles'][role]
        artifact = (name, archive['zipSha256'])
        if artifact in role_refs:
            continue
        if len(role_refs) >= max_role_references:
            group['referencesOmitted'] += 1
            continue
        role_refs[artifact] = dict(basename=name, archiveSha256=archive['zipSha256'],
            memberReadCoverageComplete=archive['memberReadCoverageComplete'],
            filenameAttempt=None if attempt is None else int(attempt), filenameAttemptTick=tick, filenameArchiveSequence=sequence)
    pairs = []
    for group in groups.values():
        finals = list(group['roles']['final-name'].values())
        attempts = list(group['roles']['legacy-attempt-name'].values())
        if not finals or not attempts:
            continue
        distinct = any(final['archiveSha256'] != attempt['archiveSha256'] for final in finals for attempt in attempts)
        state = ('distinct-archive-bytes' if distinct else
                 'reference-limit-or-same-archive-bytes' if group['referencesOmitted'] else 'same-archive-bytes-under-both-names')
        pairs.append(dict(operation=group['operation'], finalBasename=group['finalBasename'],
            availabilityMemberSha256=group['availabilityMemberSha256'], state=state, finalNameReferences=finals,
            legacyAttemptNameReferences=attempts, referencesOmitted=group['referencesOmitted']))
    return dict(scope='supplied-basenames-and-identical-selected-availability-bytes;not-desktop-inventory-or-move-proof',
                maxGroups=max_groups, maxReferencesPerRole=max_role_references, inputArchivesOmittedByGroupLimit=groups_limited,
                pairs=pairs, desktopFileCountProven=False, cleanupFailureCauseProven=False)


WINDOWS_DEADLINE_OBSERVATION_STATES = {
    'commandCapturePipe': ('reported-capture-loop-pipe',),
    'deadlineJobAccounting': ('reported-job-accounting', 'reported-query-failure'),
    'deadlineRootAccounting': ('reported-root-accounting', 'reported-query-failure'),
    'deadlineReportPath': ('reported-attribute-lookup',),
}


def windows_query_cohort_record(transaction):
    query = transaction['windowsQuery'] or {}
    receipt = query.get('reported', {})
    outcome = query.get('outcomeAndTiming', {})
    current = outcome.get('current', {})
    previous = query.get('recentDeadline', {})
    phase = transaction['preSnapshotPhaseTiming'] or {}
    elapsed = phase.get('elapsedMs', {}) if phase.get('state') == 'reported-completed-phases' else {}
    reason, attempts, report = (receipt.get(k) for k in ('reason', 'queryAttempts', 'reportAvailable'))
    fields = current.get('reported', {})
    receipt_state = query.get('state')
    issues = query.get('fieldIssues', ())
    if (receipt_state == 'invalid-fields' and issues and
            all(issue.partition(':')[0] in WINDOWS_DEADLINE_OBSERVATION_STATES for issue in issues)):
        # Optional diagnostics do not change an independently valid query outcome.
        receipt_state = 'reported'
    state = 'unclassified'
    if receipt_state == 'reported-skipped-by-request':
        state = 'historical-skip-by-request'
    elif receipt_state == 'reported':
        if outcome.get('state') == 'missing' and (receipt.get('status'), report) == ('UNKNOWN', False):
            if reason == 'export-context-not-current' and attempts == 0:
                state = 'context-not-current'
            elif reason == 'export-context-changed' and attempts in (0, 1):
                state = 'context-changed'
        elif (outcome.get('state') == 'missing' and reason == 'no-qualifying-historical-anomaly' and
              receipt.get('status') == 'NOT_REQUIRED' and attempts == 0 and report is False):
            state = 'historical-no-qualifying-anomaly'
        elif outcome.get('state') == 'reported-code-and-time-domains':
            if current.get('state') == 'suppressed-current-query-not-executed':
                state = 'cooldown-no-current-query'
            elif current.get('state') == 'reported-code-and-time-domains' and not previous:
                if (reason == 'diagnostic-report-available' and receipt.get('status') == 'AVAILABLE' and
                        report is True and fields['completion'] == 'child-exit' and fields['exitCode'] == 0):
                    state = 'report-available'
                elif receipt.get('status') == 'UNKNOWN' and report is False:
                    if reason == 'query-not-completed-successfully' and fields['completion'] == 'deadline':
                        state = 'capture-deadline'
                    elif fields['completion'] != 'deadline' and reason in ('query-not-completed-successfully',
                            'query-exception', 'report-read-failed', 'report-empty-oversize-or-invalid-prefix'):
                        state = 'other-attempt-outcome'
    used = state in ('report-available', 'capture-deadline', 'other-attempt-outcome')
    before = EXPORT_TIMING_PHASES[:3]
    return dict(state=state, receiptState=receipt_state, reason=reason, queryAttempts=attempts,
                currentQuery=fields if used else None, timeoutMs=receipt.get('timeoutMs'),
                currentQueryPhasesMs=query.get('queryPhaseElapsedMs', {}).get('reported') or None,
                deadlinePolicy=query.get('deadlineRetryPolicy', {}).get('reported') or None,
                previousDeadline=previous if state == 'cooldown-no-current-query' else None,
                windowsPhaseElapsedMs=elapsed.get('windowsSystemPowerReport'),
                preQueryPhasesMs=sum(elapsed[k] for k in before) if all(k in elapsed for k in before) else None,
                postQueryPhaseMs=elapsed.get('remainingStateAndManifest'))


def windows_query_scheduling_cohort_record(transaction):
    scheduling = transaction.get('windowsSleepScheduling') or {}
    timing = transaction.get('preSnapshotPhaseTiming') or {}
    phases = timing.get('elapsedMs', {}) if timing.get('state') == 'reported-completed-phases' else {}
    state = scheduling.get('state', 'missing')
    combined, manifest = (phases.get(k) for k in ('windowsReportAndRemainingState', 'manifestWrite'))
    if ((state == 'reported-overlap-intervals' or combined is not None or manifest is not None) and
            any(k in phases for k in ('windowsSystemPowerReport', 'remainingStateAndManifest'))):
        return dict(state='mixed-legacy-and-overlap-phases', intervals=None, combinedWallMs=None, manifestWriteMs=None)
    # Manifest identity intervals are contained in remainingStateMs, not additional wall time.
    return dict(state=state, intervals=scheduling['reported'] if state == 'reported-overlap-intervals' else None,
                combinedWallMs=combined, manifestWriteMs=manifest)


def cooldown_query_receipt_comparison(entry, operations):
    recent = entry['observation']['previousDeadline']
    state, origin = cooldown_origin_operation(recent, entry['operation'])
    view = dict(state=state, originOperation=origin, comparedPolicyFields=[], conflictingFields=[],
                scope='named-origin-availability-receipt;exact-operation-and-writer;historical-query-only')
    if origin is None:
        return view
    witness = operations.get(CrossArchiveExportTerminals.key(origin))
    if witness is None:
        return dict(view, state='origin-receipt-not-observed')
    if witness['conflictingReceipts']:
        return dict(view, state='conflicting-origin-receipts')
    observed = witness['observation']
    if observed['state'] != 'capture-deadline':
        if observed['currentQuery'] is None:
            return dict(view, state='origin-query-unverified')
        return dict(view, state='reported-origin-fields-conflict', conflictingFields=['query.completion'])
    view['conflictingFields'] = [f'query.{key}' for key, value in observed['currentQuery'].items()
                                 if recent['query'][key] != value]
    policy = observed['deadlinePolicy'] or {}
    for key in ('completedTickMs', 'powerGeneration', 'cooldownMs', 'deadlineStreak', 'streakWindowMs'):
        if key in recent and key in policy:
            view['comparedPolicyFields'].append(key)
            if recent[key] != policy[key]:
                view['conflictingFields'].append(f'policy.{key}')
    view['state'] = 'reported-origin-fields-conflict' if view['conflictingFields'] else 'reported-origin-fields-agree'
    return view


def cross_archive_windows_query_cohort(archives, *, max_operations=4096, max_witnesses=8):
    """Count selected query projections once per full operation and reported writer lifetime."""
    seen, operations = set(), {}
    excluded, counts, schedule_counts = collections.Counter(), collections.Counter(), collections.Counter()
    deadline_counts = {name: collections.Counter() for name in WINDOWS_DEADLINE_OBSERVATION_STATES}
    for archive in archives:
        sha = archive['zipSha256']
        if sha in seen:
            continue
        seen.add(sha)
        local = archive['commonEvidence']['ordinaryExportTransaction']
        identity = local['operation']
        if local['receiptSelectionStatus'] != 'selected':
            excluded['availability-not-selected'] += 1
            continue
        if identity['state'] != 'reported-process-operation' or local['state'] == 'operation-unsupported-fields':
            excluded[local['state']] += 1
            continue
        operation = identity['reported']
        key = CrossArchiveExportTerminals.key(operation)
        if key not in operations and len(operations) >= max_operations:
            excluded['operation-index-limit'] += 1
            continue
        observation = windows_query_cohort_record(local)
        scheduling = windows_query_scheduling_cohort_record(local)
        entry = operations.setdefault(key, dict(operation=operation, observation=observation,
            windowsReportScheduling=scheduling, conflictingScheduling=False,
            receiptArchives=0, archiveSha256Witnesses=[], conflictingReceipts=False))
        if entry['observation'] != observation:
            entry['conflictingReceipts'] = True
        if entry['windowsReportScheduling'] != scheduling:
            entry['conflictingScheduling'] = True
        if observation['state'] == 'capture-deadline':
            query = local['windowsQuery']
            details = entry.setdefault('currentDeadlineObservations', {})
            for name, valid in WINDOWS_DEADLINE_OBSERVATION_STATES.items():
                item = query.get(name, {})
                state = item.get('state', 'missing')
                if state == 'missing':
                    continue
                field = details.setdefault(name, dict(value=None, reportedArchives=0, invalidArchives=0, conflicting=False))
                if state not in valid:
                    field['invalidArchives'] += 1
                else:
                    if field['reportedArchives']:
                        field['conflicting'] |= field['value'] != item
                    else:
                        field['value'] = item
                    field['reportedArchives'] += 1
        entry['receiptArchives'] += 1
        if len(entry['archiveSha256Witnesses']) < max_witnesses:
            entry['archiveSha256Witnesses'].append(sha)
    for entry in operations.values():
        if entry['conflictingReceipts']:
            entry['observation'] = None
            entry.pop('currentDeadlineObservations', None)
            counts['conflicting-receipts'] += 1
        else:
            counts[entry['observation']['state']] += 1
            if entry['observation']['state'] == 'cooldown-no-current-query':
                entry['cooldownOriginReceipt'] = cooldown_query_receipt_comparison(entry, operations)
            if entry['observation']['state'] == 'capture-deadline':
                details = entry['currentDeadlineObservations']
                for name, counter in deadline_counts.items():
                    field = details.get(name)
                    if field is None:
                        counter['missing'] += 1
                    elif field['conflicting']:
                        counter['conflicting-receipts'] += 1
                        field['value'] = None
                    elif field['value'] is None:
                        counter['invalid-fields'] += 1
                    else:
                        counter[field['value']['state']] += 1
        if entry['conflictingScheduling']:
            entry['windowsReportScheduling'] = None
            schedule_counts['conflicting-receipts'] += 1
        else:
            scheduling = entry['windowsReportScheduling']
            mode = scheduling['intervals']['mode'] if scheduling['state'] == 'reported-overlap-intervals' else scheduling['state']
            schedule_counts[mode] += 1
    return dict(scope='selected-availability-receipts-only;distinct-reported-operations;not-all-exports-or-sleep-outcomes',
                uniqueArchives=len(seen), excludedArchives=dict(excluded), distinctOperations=len(operations),
                counts=dict(counts), operations=list(operations.values()), maxOperations=max_operations,
                schedulingCounts=dict(schedule_counts),
                schedulingScope='distinct-operation;overlapping-intervals-not-additive;not-measured-speedup-or-query-result',
                currentDeadlineObservationCounts={name: dict(counter) for name, counter in deadline_counts.items()},
                currentDeadlineObservationScope='distinct-current-deadline-operations;missing-is-unknown;no-wait-cause-inferred',
                maxArchiveWitnessesPerOperation=max_witnesses)


class CrossArchiveExportTerminals:
    """Collect bounded terminal evidence from already parsed Native carriers."""
    MAX_OPERATIONS = 4096
    MAX_WITNESSES = 8

    def __init__(self):
        self.operations = {}
        self.seen_archives = set()
        self.rows_beyond_limit = 0

    @staticmethod
    def key(operation):
        return (operation['id'], operation['writerPid'], operation['writerProcessCreation100ns'],
                operation['ordinal'], operation['startedTickMs'])

    def observe(self, archive_sha, members):
        if archive_sha in self.seen_archives:
            return
        self.seen_archives.add(archive_sha)
        for filename, rows, metadata in members:
            if not re.fullmatch(r'native-(?:lifecycle|detail)\.log(?:\.(?:prev|previous|\d+))?', filename):
                continue
            for line, row in rows:
                if row.get('event') != 'log-export-result':
                    continue
                operation = row.get('exportOperation')
                if (not isinstance(operation, dict) or set(operation) != ORDINARY_EXPORT_OPERATION_FIELDS or
                        export_operation_record(row)['state'] != 'reported-process-operation'):
                    continue
                key = self.key(operation)
                entry = self.operations.get(key)
                if entry is None:
                    if len(self.operations) >= self.MAX_OPERATIONS:
                        self.rows_beyond_limit += 1
                        continue
                    entry = self.operations[key] = dict(rows=0, archives=0, lastArchive=None,
                        duplicateInArchive=False, writerConflicts=0, projectionsDiffer=False,
                        terminal=None, archiveTiming=None, archiveTimingDiffers=False,
                        archiveSnapshotWork=None, archiveSnapshotWorkDiffers=False,
                        archiveChecks=None, archiveChecksDiffer=False, rawFinalOutputReported=False, witnesses=[])
                entry['rows'] += 1
                if entry['lastArchive'] == archive_sha:
                    entry['duplicateInArchive'] = True
                else:
                    entry['lastArchive'] = archive_sha
                    entry['archives'] += 1
                exact_writer = (type(row.get('pid')) is int and row['pid'] == operation['writerPid'] and
                                row.get('writerProcessCreation100ns') == operation['writerProcessCreation100ns'])
                if not exact_writer:
                    entry['writerConflicts'] += 1
                else:
                    projection = ordinary_export_terminal_record(row)
                    timing, checks, snapshot_work = cross_archive_terminal_details(row)
                    entry['rawFinalOutputReported'] |= 'reportedFinalOutputAfterMove' in checks
                    if entry['terminal'] is None:
                        entry['terminal'] = projection
                        entry['archiveTiming'] = timing
                        entry['archiveChecks'] = checks
                        entry['archiveSnapshotWork'] = snapshot_work
                    elif entry['terminal'] != projection:
                        entry['projectionsDiffer'] = True
                    if entry['archiveTiming'] != timing:
                        entry['archiveTimingDiffers'] = True
                    if entry['archiveChecks'] != checks:
                        entry['archiveChecksDiffer'] = True
                    if entry['archiveSnapshotWork'] != snapshot_work:
                        entry['archiveSnapshotWorkDiffers'] = True
                if len(entry['witnesses']) < self.MAX_WITNESSES:
                    entry['witnesses'].append(dict(archiveSha256=archive_sha, file=filename[:128], line=line,
                        archiveEntryOccurrence=metadata.get('archiveEntryOccurrence'), sameRowWriterMatched=exact_writer))

    def evidence(self, archives):
        carries = self.preceding_carries(archives)
        receipts = []
        seen = set()
        for archive in archives:
            sha = archive['zipSha256']
            if sha in seen:
                continue
            seen.add(sha)
            local = archive['commonEvidence']['ordinaryExportTransaction']
            operation = local['operation']
            if (operation['state'] != 'reported-process-operation' or
                    local['state'] == 'operation-unsupported-fields'):
                continue
            operation = operation['reported']
            entry = self.operations.get(self.key(operation))
            absent_count = None if self.rows_beyond_limit else 0
            view = dict(receiptArchiveSha256=sha, operation=operation, localState=local['state'],
                        state='index-limit-or-terminal-not-observed' if self.rows_beyond_limit else 'terminal-not-observed',
                        terminalRows=absent_count, terminalArchives=absent_count, terminal=None, archiveTiming=None, archiveChecks=None,
                        archiveSnapshotWork=None,
                        witnesses=[], omittedWitnesses=absent_count)
            if entry is not None:
                rows, sources = entry['rows'], entry['archives']
                witnesses = entry['witnesses']
                view.update(terminalRows=rows, terminalArchives=sources, witnesses=witnesses,
                            omittedWitnesses=rows-len(witnesses), writerConflictRows=entry['writerConflicts'],
                            repeatedWithinArchive=entry['duplicateInArchive'],
                            differentTerminalProjections=entry['projectionsDiffer'],
                            differentArchiveTimingProjections=entry['archiveTimingDiffers'],
                            differentArchiveSnapshotWorkProjections=entry['archiveSnapshotWorkDiffers'],
                            differentArchiveCheckProjections=entry['archiveChecksDiffer'])
                if entry['rawFinalOutputReported'] and (entry['archiveChecksDiffer'] or entry['projectionsDiffer'] or
                        entry['duplicateInArchive'] or entry['writerConflicts']):
                    view['rawFinalOutputReported'] = True
                if entry['duplicateInArchive'] or entry['writerConflicts'] or entry['projectionsDiffer']:
                    view['state'] = 'ambiguous-or-conflicting-terminal'
                elif sources == 1:
                    view['state'] = ('terminal-in-receipt-archive' if witnesses[0]['archiveSha256'] == sha
                                     else 'terminal-in-another-archive')
                    view['terminal'] = entry['terminal']
                else:
                    view.update(state='consistent-projection-in-multiple-archives', terminal=entry['terminal'])
                if view['terminal'] is not None and not entry['archiveTimingDiffers']:
                    view['archiveTiming'] = entry['archiveTiming']
                if view['terminal'] is not None and not entry['archiveChecksDiffer']:
                    view['archiveChecks'] = entry['archiveChecks']
                if view['terminal'] is not None and not entry['archiveSnapshotWorkDiffers']:
                    view['archiveSnapshotWork'] = entry['archiveSnapshotWork']
            view['modeEvidence'] = ordinary_export_mode_evidence(local['modeEvidence']['manifest'], local,
                                                                view['terminal'], view['state'])
            carry = carries['operations'].get(self.key(operation))
            if carry is not None:
                view['precedingCarryFromOtherArchive'] = self.carry_view(carry, view)
            boundary = archive['commonEvidence']['ordinaryExportSnapshotBoundary']
            checkpoint_comparison = export_snapshot_timing_comparison(boundary, view)
            if checkpoint_comparison is not None:
                view['snapshotTimingComparison'] = checkpoint_comparison
            bound = boundary.get('completedWallLowerBound')
            if bound is not None:
                wall_comparison = export_checkpoint_wall_terminal_comparison(
                    bound, local['preSnapshotPhaseTiming']['elapsedMs'], view, checkpoint_comparison)
                if wall_comparison is not None:
                    view['completedWallLowerBoundComparison'] = wall_comparison
            size_comparison = export_output_size_comparison(view, archive.get('zipSizeBytes'))
            if size_comparison is not None:
                view['outputSizeComparison'] = size_comparison
            receipts.append(view)
        return dict(scope='selected-receipts-and-retained-parsed-native-rows;exact-operation-and-writer-only',
                    uniqueArchives=len(seen), maxOperations=self.MAX_OPERATIONS,
                    operationsIndexed=len(self.operations), rowsBeyondOperationLimit=self.rows_beyond_limit,
                    maxWitnessesPerOperation=self.MAX_WITNESSES, receipts=receipts,
                    precedingCarryCoverage=dict(operationsIndexed=len(carries['operations']), carriersBeyondOperationLimit=carries['omitted']),
                    legacyPublicAttemptPairs=legacy_public_archive_pairs(archives),
                    windowsQueryCohort=cross_archive_windows_query_cohort(archives, max_operations=self.MAX_OPERATIONS,
                                                                        max_witnesses=self.MAX_WITNESSES),
                    allArchiveMembersRead=all(a['memberReadCoverageComplete'] for a in archives),
                    matchingProjectionProvesSamePhysicalRow=False, desktopFinalZipCountOrIpcDeliveryProven=False)

    # A None field keeps the whole validated detail when it has no nested payload.
    CARRY_DETAILS = {'previousArchiveAttempt': (('reported-previous-failed-attempt',), 'reported'),
                     'finalOutputAfterMove': (('reported-final-check-after-move',), 'reported'),
                     'archiveAttemptSteps': (('reported-completed-steps',), 'attempts'),
                     'archiveChildFailures': (('reported-archive-child-failures',), 'failures'),
                     'archiveTemporaryLookups': (('reported-archive-temporary-lookups',), 'lookups'),
                     'snapshotCopyFailureEvidence': (PRIOR_COPY_FAILURE_STATES, 'value'),
                     'rendererReply': (('reported-matching-preceding-renderer-reply',), None)}

    def preceding_carries(self, archives):
        seen, operations, omitted = set(), {}, 0
        for archive in archives:
            sha = archive['zipSha256']
            if sha in seen:
                continue
            seen.add(sha)
            local = archive['commonEvidence']['ordinaryExportTransaction']
            carry = local['precedingOperationCarry']
            if carry is None or carry['state'] != 'reported-in-memory-preceding-terminal':
                continue
            key = self.key(carry['operation'])
            if key not in operations and len(operations) >= self.MAX_OPERATIONS:
                omitted += 1
                continue
            core = {name: carry[name] for name in ('terminal', 'bodyResultOk', 'exceptionClass', 'win32Error')}
            entry = operations.setdefault(key, dict(core=core, coreConflicts=False,
                                                     carrierArchives=0, witnesses=[], details={}))
            entry['carrierArchives'] += 1
            entry['coreConflicts'] |= entry['core'] != core
            if len(entry['witnesses']) < self.MAX_WITNESSES:
                entry['witnesses'].append(dict(archiveSha256=sha, operation=local['operation']['reported'],
                    availabilityOccurrence=local['archiveEntryOccurrence'], localRowComparison=carry['retainedRowComparison']))
            for name, (valid, field) in self.CARRY_DETAILS.items():
                detail = carry.get(name)
                if detail is None:
                    continue
                state = entry['details'].setdefault(name, dict(value=None, reportedArchives=0,
                                                                unusableArchives=0, conflicting=False))
                if detail['state'] not in valid:
                    state['unusableArchives'] += 1
                    continue
                if name in ('snapshotCopyFailureEvidence', 'archiveChildFailures', 'archiveTemporaryLookups', 'finalOutputAfterMove'):
                    state['conflicting'] |= detail['retainedRowComparison'] == 'reported-fields-conflict'
                value = detail if field is None else detail[field]
                if state['reportedArchives']:
                    if name in ('archiveChildFailures', 'archiveTemporaryLookups'):
                        merged = {row['attempt']: row for row in state['value']}
                        for row in value:
                            number = row['attempt']
                            if number in merged:
                                state['conflicting'] |= merged[number] != row
                            else:
                                merged[number] = row
                        state['value'] = [merged[number] for number in sorted(merged)]
                    else:
                        state['conflicting'] |= state['value'] != value
                else:
                    state['value'] = value
                state['reportedArchives'] += 1
        return dict(operations=operations, omitted=omitted)

    def carry_view(self, entry, view):
        conflicts, core = entry['coreConflicts'], entry['core']
        terminal = view['terminal']
        if conflicts:
            comparison = 'conflicting-carried-terminals'
        elif terminal is None:
            comparison = view['state']
        else:
            # Compare overlapping projected fields without using the carry as a Native row.
            carried = core['terminal']
            fields = ((carried['reported'], terminal['reported']),
                (carried['phaseTiming']['elapsedMs'], terminal['phaseTiming']['elapsedMs']),)
            if any(raw[k] != v for compact, raw in fields for k,v in compact.items() if k in raw):
                comparison = 'reported-core-fields-conflict'
            elif any(compact.keys() - raw.keys() for compact, raw in fields):
                comparison = 'native-core-fields-incomplete'
            else:
                comparison = 'reported-core-fields-agree'
        details = {name: dict(detail, value=None if conflicts or detail['conflicting'] else detail['value'])
                   for name, detail in entry['details'].items()}
        return dict(state='conflicting-carried-terminals' if conflicts else 'reported-preceding-carry',
                    carrierArchives=entry['carrierArchives'], witnesses=entry['witnesses'],
                    omittedWitnesses=entry['carrierArchives']-len(entry['witnesses']),
                    core=None if conflicts else core, details=details, nativeTerminalProjectionComparison=comparison,
                    originalLogRow=False, desktopFinalZipCountOrIpcDeliveryProven=False)


def link_staged_tail_witnesses(candidates, names, receipt):
    """Correlate retained evidence only; the receipt has no staged content hash."""
    if (not candidates or not isinstance(receipt, dict) or type(receipt.get('schemaVersion')) is not int or
            receipt['schemaVersion'] != 1 or receipt.get('evidenceScope') != 'actual-staged-files' or
            receipt.get('tailBytesRetained') is not True or not isinstance(receipt.get('files'), list)):
        return
    counts = collections.Counter(name for name in names if name in candidates)
    staged = collections.defaultdict(list)
    for index, row in enumerate(receipt['files']):
        if isinstance(row, dict) and isinstance(row.get('entry'), str) and row['entry'] in candidates:
            staged[row['entry']].append((index, row))
    for name, (warning, size, tail) in candidates.items():
        if counts[name] != 1 or len(staged[name]) != 1:
            continue
        index, row = staged[name][0]
        if (row.get('evidenceScope') != 'actual-staged-bytes-not-live-source' or
                row.get('status') != 'snapshot-partial-tail-retained' or row.get('originalBytesRetained') is not True or
                type(row.get('bytes')) is not int or row['bytes'] != size or
                copy_staged_boundary_evidence(row)['state'] != 'reported-partial-tail'):
            continue
        warning['stagedTailEvidence'] = {
            'state': 'receipt-and-member-agree-on-tail-shape', 'receipt': 'log-export-snapshot.json',
            'receiptOccurrence': 0, 'fileRowIndex': index, 'archiveEntryOccurrence': 0, 'memberBytes': size,
            'memberLine': tail['line'], 'memberLineSha256': tail['rawLineSha256'],
            'scope': 'exact-unique-root-name-and-size;staged-receipt-reports-tail-before-compression',
            'stagedBytesComparedToMember': False, 'copyCutOrSourceDamageCauseProven': False}


def monitor_tail_separator_evidence(availability, names, warnings):
    monitor = availability.get('monitorDetail')
    if not isinstance(monitor, dict) or 'tailSeparatorEvidence' not in monitor:
        return None
    receipt = monitor['tailSeparatorEvidence']
    view = dict(state='invalid-fields', accepted=None, last=None, offsetMatches=[],
                scope='retained-malformed-lines-at-reported-lf-offset;not-file-identity-or-causation',
                fileIdentityProven=False)
    if (not isinstance(receipt, dict) or set(receipt) != {'schemaVersion', 'accepted', 'last'} or
            type(receipt['schemaVersion']) is not int or receipt['schemaVersion'] != 1):
        return view
    def uint(value):
        return type(value) is int and 0 <= value < (1 << 64)
    count, last = receipt['accepted'], receipt['last']
    if not uint(count):
        return view
    if count == 0 and last is None:
        return dict(view, state='reported-none-accepted', accepted=0)
    if (count == 0 or not isinstance(last, dict) or set(last) != {'tick', 'offsetBytes'} or
            not uint(last['tick']) or not uint(last['offsetBytes']) or last['offsetBytes'] == 0):
        return view
    offset = last['offsetBytes']
    comparable = {name for name in ('monitor-detail.log', 'monitor-detail.log.1') if names.count(name) == 1}
    for warning in warnings:
        if warning['file'] not in comparable:
            continue
        for row in warning.get('invalidLineByteEvidence', {}).get('observations', []):
            if (row['kind'] == 'invalid-json-row' and row['terminatedByLf'] and
                    row['byteOffset'] + row['byteLength'] == offset):
                view['offsetMatches'].append(dict(file=warning['file'], archiveEntryOccurrence=0,
                    line=row['line'], rawLineSha256=row['rawLineSha256']))
    return dict(view, state='reported-stream-accepted', accepted=count, last=last)


def audit_zip(path, policy, *, cross_archive_exports=None):
    ui_action_links = UiActionLinkEvidence()
    result = {'zipPath': str(path),
              'crcFirstBadEntry': None, 'manifest': {}, 'files': [], 'violations': [],
              'eventVolumeCandidates': [], 'transactionSummaries': [], 'nativeProcessCostSummaries': [], 'coverageWarnings': [], 'excludedExternalFiles': [],
              'memberReadFailures': [], 'memberReadCoverageComplete': True}
    with path.open('rb') as stream, ExitStack() as stack:
        initial_stat = os.fstat(stream.fileno())
        archive_hash = hashlib.file_digest(stream, 'sha256').hexdigest()
        result['zipSha256'] = archive_hash
        archive = stack.enter_context(zipfile.ZipFile(stream))
        names = archive.namelist()
        steam_mirrors = SteamTailMirrorAudit(archive.infolist())
        metadata_names = AUDIT_RECEIPT_NAMES
        reserved = (AUDIT_MANIFEST_NAME,) + metadata_names
        occurrences = collections.Counter(name for name in names if name in reserved)
        virtual_member_counts = collections.Counter(name for name in names if name in (VIRTUAL_JOURNAL, VIRTUAL_SESSION))
        virtual_journal_member = virtual_session_member = None
        selections = {name: {'file': name, 'archiveEntries': occurrences[name],
            'status': 'missing' if not occurrences[name] else 'uninspected' if occurrences[name] == 1 else 'ambiguous-repeated-entry',
            'selectedOccurrence': None, 'referenceCount': 0, 'references': [], 'referencesOmitted': 0} for name in reserved}
        result['metadataSelectionEvidence'] = {'scope': 'retained-exact-entry-selection;not-content-authentication-or-runtime-proof',
            'parseByteLimit': MAX_AUDIT_RECEIPT_BYTES, 'maxReferencesPerEntry': MAX_AUDIT_RECEIPT_REFERENCES,
            'duplicateJsonKeysRejected': True, 'documents': list(selections.values())}
        for name in reserved:
            if occurrences[name] > 1:
                result['coverageWarnings'].append({'file': name, 'metadataSelection': 'ambiguous-repeated-entry',
                                                   'archiveEntries': occurrences[name]})
        def reference(name, occurrence, data=None, failure=None):
            state = selections[name]
            state['referenceCount'] += 1
            row = {'archiveEntryOccurrence': occurrence, 'readOutcome': 'failed' if failure else 'read',
                   'bytes': None if data is None else len(data),
                   'sha256': None if data is None else hashlib.sha256(data).hexdigest()}
            if failure:
                row.update(stage=failure['stage'], errorType=failure['errorType'])
            refs = state['references']
            if len(refs) < MAX_AUDIT_RECEIPT_REFERENCES:
                refs.append(row)
            else:
                refs[MAX_AUDIT_RECEIPT_REFERENCES // 2:] = refs[MAX_AUDIT_RECEIPT_REFERENCES // 2 + 1:] + [row]
            state['referencesOmitted'] = state['referenceCount'] - len(refs)
        manifest = ''
        manifest_data = None
        state = selections[AUDIT_MANIFEST_NAME]
        manifest_infos = [info for info in archive.infolist() if info.filename == AUDIT_MANIFEST_NAME]
        if len(manifest_infos) == 1:
            if manifest_infos[0].file_size > MAX_AUDIT_RECEIPT_BYTES:
                state['status'] = 'parse-byte-budget-exceeded'
                result['coverageWarnings'].append({'file': AUDIT_MANIFEST_NAME, 'metadataSelection': state['status']})
            else:
                try:
                    manifest_data = archive.read(manifest_infos[0])
                    manifest = manifest_data.decode('utf-8-sig', errors='replace')
                    state.update(status='selected', selectedOccurrence=0)
                except ZIP_READ_ERRORS:
                    state['status'] = 'read-failed'  # The normal traversal retains the actual failure.
        pairs = re.findall(r'^([\w.-]+)=(.*)$', manifest, re.M)
        fields = {key: value.strip() for key, value in pairs}
        field_counts = collections.Counter(key for key, _ in pairs if key in AUDIT_MANIFEST_FIELDS)
        duplicates = {key: count for key, count in field_counts.items() if count > 1}
        if duplicates:
            for key in duplicates:
                fields.pop(key, None)
            state.update(status='selected-with-ambiguous-fields', ambiguousFieldOccurrences=duplicates)
            result['coverageWarnings'].append({'file': AUDIT_MANIFEST_NAME, 'ambiguousManifestFields': duplicates})
        manifest_mode = ordinary_export_manifest_mode(fields, state)
        result['manifest'] = {key: fields.get(key) for key in AUDIT_MANIFEST_FIELDS}
        result['manifest']['logExportMode'] = manifest_mode['reportedMode']
        current_pid = fields.get('processId')
        common_members=[]
        host_exit_members=[]
        ui_presentation_members=[]
        gyro_power_members=[]
        domain_writer_envelopes=DomainWriterEnvelopeEvidence(current_pid)
        input_source_readback=InputSourceReadbackEvidence()
        fan_native_writer_envelopes=FanNativeWriterEnvelopeEvidence(current_pid)
        fan_host_instance_members=[];fan_host_instance_members_omitted=0
        process_export_envelopes=ProcessExportEnvelopeEvidence()
        cost_compactions=CostCompactionEvidence()
        virtual_recovery_firsts=VirtualRecoveryFirstWitnessEvidence()
        recovery_timing_members=[]
        common_availability={}
        touchpad_snapshot=None
        recovery_documents=[]
        omitted_recovery_documents=0
        metadata_blobs={}
        copy_receipts={}
        staged_tail_candidates={}
        def member_failure(failure):
            result['memberReadFailures'].append(failure)
            result['memberReadCoverageComplete']=False
            if failure['stage']=='outer-member-read':
                if failure['zipIntegrityFailure'] and result['crcFirstBadEntry'] is None:
                    result['crcFirstBadEntry']=failure['entry']
                if failure['entry'] in selections:
                    reference(failure['entry'], failure['archiveEntryOccurrence'], failure=failure)
                    if occurrences[failure['entry']] == 1:
                        selections[failure['entry']]['status'] = 'read-failed'
                    if failure['entry'] in metadata_names:
                        metadata_blobs[failure['entry']] = None
        for name, data, member_metadata in iter_log_members(archive,on_member_error=member_failure):
            steam_mirrors.observe(name, data, member_metadata)
            if name in selections:
                reference(name, member_metadata['archiveEntryOccurrence'], data=data)
            if name in metadata_names and occurrences[name] == 1:
                if len(data) <= MAX_AUDIT_RECEIPT_BYTES:
                    metadata_blobs[name] = data
                else:
                    selections[name]['status'] = 'parse-byte-budget-exceeded'
                    result['coverageWarnings'].append({'file': name, 'metadataSelection': selections[name]['status']})
            if name in RECOVERY_DOCUMENTS:
                if len(recovery_documents) >= MAX_RECOVERY_DOCUMENTS:
                    omitted_recovery_documents += 1
                    continue
                document={'file': name, **member_metadata, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
                try:
                    if len(data) > MAX_RECOVERY_DOCUMENT_BYTES:
                        raise ValueError('recovery document parse byte budget exceeded;original retained')
                    document['value']=evidence_json_loads(data)
                    if not isinstance(document['value'],dict):
                        raise ValueError('recovery document must be an object')
                except (ValueError, UnicodeError) as exc:
                    document['parseError']=str(exc)
                    result['coverageWarnings'].append({'file':name,**member_metadata,'metadataParseError':str(exc),**json_rejection_evidence(exc)})
                recovery_documents.append(document)
                continue
            carrier = carrier_policy(name)
            if carrier['disposition']=='excluded-external':
                result['excludedExternalFiles'].append(name)
                continue
            if carrier['disposition']!='inspect':continue
            base=carrier['canonicalName'];lane=carrier['lane']
            entry = {'file': name, 'lane': lane, 'bytes': len(data), 'rotation': base != name, **member_metadata}
            if carrier['format']=='plain-text':
                lines = data.decode('utf-8-sig', errors='replace').splitlines()
                hits = [(i, line) for i, line in enumerate(lines, 1) if base == 'app.log' and 'rog.hid-discovery' in line]
                entry.update({'format': 'plain-text', 'lineCount': len(lines), 'rogDiscoveryRows': len(hits),
                              'processIdentity': 'not-recorded; never attributed to manifest PID'})
                if len(hits) > 1:
                    result['violations'].append({'kind': 'rog-discovery-text-bypasses-lane-router', 'file': name,
                                                  'event': 'rog.hid-discovery', 'count': len(hits),
                                                  'firstLine': hits[0][0], 'lastLine': hits[-1][0],
                                                  'currentPidMatch': None})
                result['files'].append(entry)
                continue
            rows, quality = load_rows(data,compact_quality=True)
            if base in ('native-detail.log','virtual-gamepad.log'):
                input_source_readback.observe(name,rows,member_metadata)
            if base in ('fan-host-fault.log','yeman-fan-host-runtime.log'):
                if len(fan_host_instance_members)<FanHostDiagnosticInstanceEvidence.MAX_MEMBERS:
                    hostInstances=FanHostDiagnosticInstanceEvidence();hostInstances.observe(rows)
                    fan_host_instance_members.append((name,member_metadata,hostInstances,FanHostOutcomeEvidence(rows)))
                else:fan_host_instance_members_omitted+=1
            virtual_recovery_firsts.observe(name,rows,member_metadata)
            domain_writer_envelopes.observe(name,rows,member_metadata)
            fan_native_writer_envelopes.observe(name,rows,member_metadata)
            if base == 'input-host-cost.log':
                cost_compactions.observe(name,rows,member_metadata,data)
            if base in PROCESS_EXPORT_CAPACITIES:
                envelope = process_export_envelopes.observe(name,rows,member_metadata,quality,data)
                if name == VIRTUAL_SESSION and virtual_member_counts[name] == 1 and not member_metadata['containerOnly']:
                    usable = envelope['status'] in ('consistent-complete-retained-snapshot', 'consistent-with-omissions')
                    virtual_session_member = rows if usable else (), envelope
            if name == VIRTUAL_JOURNAL and virtual_member_counts[name] == 1 and not member_metadata['containerOnly']:
                bounded = len(data) <= VIRTUAL_JOURNAL_MAX_BYTES and len(rows) <= VIRTUAL_JOURNAL_MAX_ROWS
                virtual_journal_member = rows if bounded else (), quality, len(data)
            if base == 'steam-original-recovery.jsonl':
                recovery_timing_members.append((name,rows,member_metadata,quality))
            if base in ('sleep-facts.log','steam-original-recovery.jsonl','decky-sidebar.log','native-lifecycle.log','native-detail.log','virtual-adaptation-session.jsonl','model-adaptation-session.jsonl','model-adaptation-signals-session.jsonl'):
                common_members.append((name,rows,member_metadata))
            if base=='input-host-fatal.log':host_exit_members.append((name,rows,member_metadata))
            if base in ('native-lifecycle.log','native-detail.log','webview-failures.log'):
                ui_presentation_members.append((name,rows,member_metadata))
            if base in ('native-lifecycle.log','native-detail.log','gyro-motion.log'):
                gyro_power_members.append((name,rows,member_metadata))
            entry.update({'format': 'json-lines', 'quality': quality, **row_stats(rows, current_pid, name)})
            if base in ('input-host-signal.log', 'input-host-cost.log'):
                writer_evidence = host_signal_writer_evidence(name, rows)
                if writer_evidence is not None:
                    entry['hostSignalWriterEvidence'] = writer_evidence
            if base == 'input-host-cost.log':
                budget_evidence = msi_cost_budget_evidence(name, rows)
                if budget_evidence is not None:
                    entry['msiCostBudgetEvidence'] = budget_evidence
            if base in ('native-lifecycle.log','native-detail.log'):
                entry['inputChainFrameEvidence']=input_chain_frame_evidence(rows)
            if base in ('frontend-trace.log','frontend-trace-detail.log'):
                entry['ownerTransitionEvidence']=frontend_owner_transition_evidence(rows)
                entry['executionGuardEvidence']=frontend_exec_guard_evidence(rows)
                if lane == 'frontend-detail':
                    misplaced = [(line, row) for line, row in rows
                                 if row.get('event') == 'frontend-trace' and row.get('where') == 'monitor-hud-startup']
                    if misplaced:
                        result['violations'].append({**member_metadata, 'kind': 'monitor-hud-startup-in-frontend-detail',
                                                     'file': name, 'event': 'frontend-trace',
                                                     **row_stats(misplaced, current_pid, name),
                                                     'exampleLines': [line for line, _ in misplaced[:8]]})
            ui_action_links.observe(name, rows, member_metadata)
            if quality['invalidRows'] or quality['nonObjectLines'] or quality['utf8ReplacementCharacters']:
                warning = {'file': name, **quality}
                result['coverageWarnings'].append(warning)
                if (quality['tailBoundary']['invalidUnterminatedEofRows'] == 1 and
                        '/' not in name and chr(92) not in name and not member_metadata.get('containerOnly') and
                        member_metadata['archiveEntryOccurrence'] == 0):
                    for tail in quality['invalidLineByteEvidence']['observations']:
                        if tail['unterminatedEndOfFile'] and tail['kind'] == 'partial-or-invalid-tail':
                            staged_tail_candidates[name] = (warning, len(data), tail)
                            break
            events = collections.defaultdict(list)
            for line, row in rows:
                events[event_name(row)].append((line, row))
            if base in ('gyro-motion.log', 'gyro-motion-samples.log') and events.get('gyro-status-second'):
                gyro_samples = events['gyro-status-second']
                entry['gyroReceiptAgeEvidence'] = gyro_receipt_age_evidence(gyro_samples)
                cadence = gyro_sample_cadence_evidence(gyro_samples)
                if cadence is not None:
                    entry['gyroSampleCadenceEvidence'] = cadence
            entry['events'] = [{'event': event, **row_stats(items, current_pid, name),
                                'baseRegisteredInCurrentSource': event in policy['baseEvents'] if lane == 'native-low' else None}
                               for event, items in sorted(events.items(), key=lambda x: -len(x[1]))]
            # Only the same top eight eligible groups per resident carrier get
            # fixed scalar evidence. Keep no additional raw-row references.
            if lane in RESIDENT_VOLUME_LANES:
                selected = [event for event in entry['events'] if event['counts']['all'] >= 100][:8]
                groups = {event['event']: events[event['event']] for event in selected}
                entry['_residentVolumeRowBytes'] = resident_event_byte_evidence(data, groups)
                entry['_residentVolumeReportedOk'] = {
                    event: resident_reported_ok_field_evidence(items, current_pid, name)
                    for event, items in groups.items()}
            result['files'].append(entry)
            if base == 'virtual-gamepad.log':
                result['nativeProcessCostSummaries'].append(native_process_cost_summary(rows, current_pid, name))
            if lane != 'native-low':
                continue
            # A removed record may hide an ownership/boot/power transition.
            # It invalidates sequential equality, not each retained row's facts
            # or transactions correlated by explicit immutable identifiers.
            unknown_lines = _quality_unknown_lines(quality)
            if unknown_lines:
                entry['sequentialComparisonEvidence'] = {
                    'uninterpretedLines': len(unknown_lines),
                    'firstLine': unknown_lines[0], 'lastLine': unknown_lines[-1],
                    'resetAllWriterHistory': True,
                    'scope': 'four sequential repeat classifiers only;not event absence, runtime continuity or explicit transaction invalidation'}
            result['transactionSummaries'].append({'file': name,
                'archiveEntryOccurrence': member_metadata['archiveEntryOccurrence'],
                **late_hide_episodes(rows, current_pid, name)})
            findings = collections.defaultdict(list)
            for line, row in rows:
                event = event_name(row)
                reason = None
                if (event == 'system-xinput-readback' and row.get('reason') == 'buttons-change') or event in NATIVE_DETAIL or (event == 'oem-producer-edge' and row.get('status') == 'queued' and row.get('reason') == 'state-transition') or (event == 'msi-wmi-key' and row.get('reason') == 'queued' and row.get('accepted') is True and row.get('posted') is True):
                    reason = 'known-detail-in-native-low'
                elif ((event == 'gpd-back-chord-state' and row.get('reason') == 'physical-edge') or
                      (event in ('gpd-back-chord-match', 'virtual-pad-backbuttons') and row.get('reason') in ('press', 'release', 'masked'))):
                    reason = 'known-detail-in-native-low'
                elif event == 'desktop-hud-event' and row.get('hudEvent') == 'desktopHud.painted':
                    reason = 'known-detail-in-native-low'
                elif event == 'input-host-command-result' and row.get('command') == 'SUBMIT_FRAME' and row.get('ok') is True:
                    reason = 'successful-frame-in-native-low'
                elif event in ('ipc-response-delivery-failed', 'ipc-response-delivery-exception', 'ipc-response-serialization-failed'):
                    selection = row.get('failureLogSelection')
                    if (isinstance(selection, dict) and type(selection.get('schema')) is int and selection['schema'] == 1 and
                            selection.get('reason') in ('repeat-type', 'capacity-detail', 'unclassified-detail') or
                            isinstance(selection,dict) and type(selection.get('schema')) is int and selection['schema']==2 and
                            native_ipc_failure_record(row)['selection']['laneExpectation']=='detail-or-suppressed'):
                        reason = 'ipc-repeated-or-untracked-detail-in-native-low'
                elif event == 'input-host-session-retained' and row.get('repeatObservation') is True:
                    reason = 'retained-repeat-marker-in-native-low'
                elif event == 'msi-rear-source-observation' and row.get('snapshotReason') == 'raw-input-change':
                    reason = 'input-activity-counters-in-native-low'
                if reason:
                    findings[(event, reason)].append((line, row))
            duplicates = duplicate_snapshots(rows, name, unknown_lines=unknown_lines)
            if duplicates:
                findings[('rog.hid-discovery-summary', 'unchanged-discovery-snapshot-in-native-low')] = duplicates
            guard_repeats = repeated_guard_successes(rows, name, unknown_lines=unknown_lines)
            if guard_repeats:
                findings[('oem-vendor-stack', 'unchanged-periodic-guard-success-in-native-low')] = guard_repeats
            frame_evidence = {}
            frame_repeats = repeated_frame_observations(rows, name, unknown_lines=unknown_lines, evidence=frame_evidence)
            if frame_evidence:
                entry['frameComparisonEvidence'] = frame_evidence
                result['coverageWarnings'].append({'file': name, **member_metadata,
                                                   'frameComparisonEvidence': frame_evidence})
            if frame_repeats:
                findings[('input-host-frame-evidence', 'unchanged-frame-observation-in-native-low')] = frame_repeats
            for quarantine_event, items in repeated_quarantine_observations(rows, name, unknown_lines=unknown_lines).items():
                findings[(quarantine_event, 'unchanged-quarantine-observation-in-native-low')] = items
            recovery_refusals = repeated_recovery_fallback_observations(rows, name, unknown_lines=unknown_lines)
            if recovery_refusals:
                findings[('input-host-recovery-fallback', 'unchanged-recovery-queue-refusal-in-native-low')] = recovery_refusals
            violated_events = set()
            for (event, reason), items in findings.items():
                violated_events.add(event)
                result['violations'].append({'kind': reason, 'file': name, 'event': event,
                                              **row_stats(items, current_pid, name), 'exampleLines': [i for i, _ in items[:8]]})
                if reason in ('unchanged-quarantine-observation-in-native-low', 'unchanged-recovery-queue-refusal-in-native-low',
                              'unchanged-discovery-snapshot-in-native-low', 'unchanged-periodic-guard-success-in-native-low'):
                    result['violations'][-1].update({**member_metadata,
                        'comparisonScope': 'same received reported state;not operation identity',
                        'processLifetimeProven': False, 'businessActionDuplicationInferred': False,
                        'producerEmissionCoverageProven': False})
                if reason == 'retained-repeat-marker-in-native-low':
                    result['violations'][-1].update({**member_metadata,
                        'selectionScope':'producer-reported-repeatObservation-true;not-sequential-equality-or-operation-identity',
                        'processLifetimeProven':False, 'businessActionDuplicationInferred':False,
                        'producerEmissionCoverageProven':False})
            for event, items in events.items():
                if len(items) >= 100 and event not in violated_events:
                    result['eventVolumeCandidates'].append({'file': name, 'event': event,
                                                            **row_stats(items, current_pid, name),
                                                            'interpretation': 'volume-only; not a proved lane violation'})
        for name in metadata_names:
            if metadata_blobs.get(name) is not None:
                try:
                    value = evidence_json_loads(metadata_blobs[name])
                    if not isinstance(value,dict):
                        raise ValueError('metadata document must be an object')
                    selections[name].update(status='selected', selectedOccurrence=0)
                    if name in ('log-export-snapshot.json', 'steam/steam-log-export.json'):
                        copy_receipts[name] = value
                    if name.startswith('steam/'):
                        result['steamCopyReceipt'] = {k: value.get(k) for k in ('source', 'root', 'status', 'discovered', 'copied', 'skipped', 'issues')}
                    elif name == 'log-export-availability.json':
                        common_availability=value
                        result['availabilityReceipt'] = {k: value.get(k) for k in ('evidenceScope', 'frontendLogFailureCount', 'rendererDiagnosticDeliveryFailures', 'rendererDiagnosticDeliveryEvidence', 'diagnosticDeliveryCounterEvidence', 'tdpDiagnosticDeliveryEvidence', 'nativeIpcFailureEvidence', 'monitorDetail', 'bufferedNativeLogEvidence', 'frontendTraceDetailSuppressed')}
                    elif name==TOUCHPAD_EXPORT_SNAPSHOT:
                        touchpad_snapshot=value
                    else:
                        result['snapshotReceipt'] = {k: value.get(k) for k in ('evidenceScope', 'snapshotBoundaryWarnings', 'snapshotInspectionFailures', 'tailBytesRetained')}
                except (ValueError, UnicodeError, AttributeError) as exc:
                    selections[name]['status'] = 'parse-failed'
                    result['coverageWarnings'].append({'file': name, 'metadataParseError': str(exc),**json_rejection_evidence(exc)})
        link_staged_tail_witnesses(staged_tail_candidates, names, copy_receipts.get('log-export-snapshot.json'))
        result['commonEvidence']=common_evidence_summary(common_availability,common_members,current_pid)
        separator = monitor_tail_separator_evidence(common_availability, names, result['coverageWarnings'])
        if separator is not None:
            result['commonEvidence']['monitorTailSeparatorEvidence'] = separator
        if input_source_readback.counts:
            result['commonEvidence']['inputSourceReadbackEvidence']=input_source_readback.evidence()
        result['commonEvidence']['steamTailMirrors']=steam_mirrors.evidence(copy_receipts.get('log-export-snapshot.json'))
        result['commonEvidence']['fanHostDiagnosticInstances']={
            'scope':'bounded-per-retained-carrier;not-OS-lifetime-or-global-unique-hosts',
            'maxMembers':FanHostDiagnosticInstanceEvidence.MAX_MEMBERS,'membersNotInspected':fan_host_instance_members_omitted,
            'countsDeduplicatedAcrossFiles':False,'completeProducerOrMemberCoverageProven':False,
            'files':[{'file':name[:256],'fileNameTruncated':len(name)>256,
                **{key:metadata[key] for key in ('archiveEntryOccurrence','containerOnly') if key in metadata},
                'evidence':collector.evidence(common_availability,selections['log-export-availability.json'],current_pid)}
                for name,metadata,collector,_ in fan_host_instance_members]}
        result['commonEvidence']['fanHostOutcomeEvidence']={
            'scope':'bounded-per-retained-carrier;no-cross-carrier-dedup-or-event-pairing',
            'maxMembers':FanHostDiagnosticInstanceEvidence.MAX_MEMBERS,
            'membersNotInspected':fan_host_instance_members_omitted,
            'files':[{'file':name[:256],'fileNameTruncated':len(name)>256,
                **{key:metadata[key] for key in ('archiveEntryOccurrence','containerOnly') if key in metadata},
                'evidence':outcomes.evidence()} for name,metadata,_,outcomes in fan_host_instance_members]}
        result['commonEvidence']['inputHostExitEvidence']=input_host_exit_evidence(host_exit_members)
        result['commonEvidence']['uiPresentationEvidence']=ui_presentation_evidence(ui_presentation_members,current_pid)
        result['commonEvidence']['overlayRendererProbeEvidence']=overlay_renderer_probe_evidence(ui_presentation_members)
        result['commonEvidence']['overlaySurfaceSessionEvidence']=overlay_surface_session_evidence(ui_presentation_members)
        result['commonEvidence']['gyroPowerObservationEvidence']=gyro_power_observation_evidence(gyro_power_members,current_pid)
        result['commonEvidence']['domainWriterEnvelopeEvidence']=domain_writer_envelopes.evidence()
        result['commonEvidence']['fanNativeWriterEnvelopeEvidence']=fan_native_writer_envelopes.evidence()
        result['commonEvidence']['gamepadUiGateEvidence']=gamepad_ui_gate_evidence_record(common_availability)
        result['commonEvidence']['uiActionLinkEvidence']=ui_action_links.evidence()
        result['commonEvidence']['rendererOverlaySessionEntry']=renderer_overlay_session_entry_evidence(common_availability)
        result['commonEvidence']['touchpadRecoveryEvidence']=touchpad_recovery_evidence(common_members,current_pid,touchpad_snapshot,selections[TOUCHPAD_EXPORT_SNAPSHOT])
        timing=common_availability.get('exportPhaseTiming')
        result['commonEvidence']['exportPhaseTimingEvidence']=export_phase_timing_record(timing if isinstance(timing,dict) else {},'elapsedMs')
        result['commonEvidence']['windowsExportQueryEvidence']=windows_export_query_record(common_availability)
        result['commonEvidence']['processExportEnvelopeEvidence']=process_export_envelopes.evidence()
        result['commonEvidence']['virtualJournalComparison'] = virtual_journal_comparison(
            virtual_member_counts, virtual_journal_member, virtual_session_member)
        result['commonEvidence']['costCompactionEvidence']=cost_compactions.evidence()
        result['commonEvidence']['hidHideFailureReceiptCoverage']=hidhide_failure_receipt_coverage(common_availability,selections['log-export-availability.json'],current_pid)
        result['commonEvidence']['virtualRecoveryFirstWitnessEvidence']=virtual_recovery_firsts.evidence()
        result['commonEvidence']['wakeExecutionEvidence']=wake_execution_evidence(
            common_members, collections.Counter(name for name in names if name in WAKE_EXECUTION_MEMBERS))
        copies=copy_observation_evidence(copy_receipts,selections)
        result['commonEvidence']['copyObservationEvidence']=copies
        operationSelection=selections['log-export-availability.json']
        transaction=ordinary_export_transaction_evidence(common_availability,operationSelection,common_members)
        terminal = transaction['terminal']
        transaction['modeEvidence'] = ordinary_export_mode_evidence(manifest_mode, transaction,
            terminal['terminal'] if terminal is not None else None, transaction['state'])
        result['commonEvidence']['ordinaryExportTransaction']=transaction
        if cross_archive_exports is not None:
            cross_archive_exports.observe(archive_hash, common_members)
        boundary=ordinary_export_snapshot_boundary(
            transaction,copy_receipts.get('log-export-snapshot.json'),copies['sources'][0])
        bound=export_checkpoint_wall_lower_bound(common_availability,transaction,boundary)
        if bound is not None:
            boundary['completedWallLowerBound']=bound
            terminal_view=dict(state='ambiguous-or-conflicting-terminal' if transaction['state'] in (
                'multiple-matching-operation-rows','matching-operation-writer-conflict') else transaction['state'],
                terminal=terminal['terminal'] if terminal is not None else None,
                archiveTiming=terminal['archiveTiming'] if terminal is not None else None)
            comparison=export_checkpoint_wall_terminal_comparison(bound,transaction['preSnapshotPhaseTiming']['elapsedMs'],
                terminal_view,export_snapshot_timing_comparison(boundary,terminal_view))
            if comparison is not None:
                bound['terminalComparison']=comparison
        result['commonEvidence']['ordinaryExportSnapshotBoundary']=boundary
        result['commonEvidence']['exportOperationEvidence']={
            'receiptSelectionStatus':operationSelection['status'],
            'archiveEntryOccurrence':operationSelection['selectedOccurrence'],
            'record':export_operation_record(common_availability) if operationSelection['status']=='selected' else {'state':'receipt-unavailable'},
            'terminalRowsPaired':False,'currentExportOutcomeProven':False}
        result['commonEvidence']['inputCaptureQueueEvidence']=input_capture_queue_evidence(
            common_availability,selections['log-export-availability.json'],current_pid)
        result['commonEvidence']['inputCaptureLogFailureEvidence']=input_capture_log_failure_evidence(
            common_availability,selections['log-export-availability.json'],current_pid)
        result['commonEvidence']['touchHapticSummaryPreparationEvidence']=touch_haptic_preparation_evidence(
            common_availability,selections['log-export-availability.json'],current_pid)
        result['commonEvidence']['nativeAppendFailureEvidence']=native_append_failure_evidence(
            common_availability,selections['log-export-availability.json'],current_pid)
        result['commonEvidence']['deckyLogFailureEvidence']=decky_log_failure_evidence(
            common_availability,selections['log-export-availability.json'],current_pid)
        result['commonEvidence']['fanLifecycleLogFailureEvidence']=fan_lifecycle_log_failure_evidence(
            common_availability,selections['log-export-availability.json'],current_pid)
        result['commonEvidence']['fanDetailLogFailureEvidence']=fan_detail_log_failure_evidence(
            common_availability,selections['log-export-availability.json'],current_pid)
        result['commonEvidence']['coordinatorLogFailureEvidence']=coordinator_log_failure_evidence(
            common_availability,selections['log-export-availability.json'],current_pid)
        result['commonEvidence']['coordinatorRewriteCommitEvidence']=coordinator_rewrite_commit_evidence(
            common_availability,selections['log-export-availability.json'],current_pid)
        result['commonEvidence']['criticalDiagnosticQueueEvidence']=critical_diagnostic_queue_evidence(
            manifest_data,selections[AUDIT_MANIFEST_NAME],current_pid)
        result['commonEvidence']['bufferedNativeSinkEvidence']=buffered_native_sink_evidence(
            common_availability,selections['log-export-availability.json'],current_pid)
        result['commonEvidence']['adaptationStoreCoverageEvidence']=adaptation_store_coverage_evidence(
            common_availability,selections['log-export-availability.json'])
        result['commonEvidence']['costRetentionEvidence']=cost_retention_evidence(
            common_availability,selections['log-export-availability.json'],result['files'])
        result['commonEvidence']['recoveryTimingEvidence']=recovery_timing_evidence(recovery_timing_members,current_pid)
        result['commonEvidence']['recoveryRouteEvidence']=recovery_route_evidence(
            recovery_documents,common_members,current_pid,omitted_recovery_documents)
        result['commonEvidence']['memberReadCoverage']={
            'complete':result['memberReadCoverageComplete'],
            'failureCount':len(result['memberReadFailures']),
            'failedMembers':[{key:failure[key] for key in ('entry','archiveEntryOccurrence','stage')}
                             for failure in result['memberReadFailures']],
            'scope':'retained archive members;CRC/integrity on successful reads;no failed payload synthesized'}
        final_stat = os.fstat(stream.fileno())
        result['zipSizeBytes'] = (initial_stat.st_size if (initial_stat.st_size, initial_stat.st_mtime_ns) ==
                                  (final_stat.st_size, final_stat.st_mtime_ns) else None)
    result['residentEventVolumeEvidence'] = resident_event_volume_evidence(result['files'])
    for entry in result['files']:
        entry.pop('_residentVolumeReportedOk', None)
        entry.pop('_residentVolumeRowBytes', None)
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
        'Successful member reads verify ZIP CRC; failed reads/volume validation remain explicit gaps and never become empty logs or successful resource-return proof.',
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
    cross_archive_exports = CrossArchiveExportTerminals() if len(args.zip) > 1 else None
    report = {'schemaVersion': 6,
              'generatedAt': datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=8))).isoformat(),
              'scope': 'READ_ONLY_ARCHIVES_AND_ADVISORY_CURRENT_SOURCE_POLICY',
              'source': {'path': str(args.source), 'sha256': hashlib.sha256(raw).hexdigest(), **policy},
              'archives': [audit_zip(path, policy, cross_archive_exports=cross_archive_exports) for path in args.zip]}
    if cross_archive_exports is not None:
        report['crossArchiveOrdinaryExports'] = cross_archive_exports.evidence(report['archives'])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, allow_nan=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'output': str(args.output), 'archives': len(report['archives']),
                      'baseEventCount': policy['baseEventCount'],
                      'violations': sum(len(x['violations']) for x in report['archives']),
                      'coverageWarnings': sum(len(x['coverageWarnings']) for x in report['archives'])}, ensure_ascii=False))


if __name__ == '__main__':
    main()
