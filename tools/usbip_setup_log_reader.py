"""Resolve compact USBIP setup snapshots from the two retained root log members."""
from __future__ import annotations

import argparse
import base64
import collections
import hashlib
import io
import json
from pathlib import Path
import re
import zipfile
import zlib

from ai_log_reader import MAX_MEMBERS, evidence_json_loads, validated_ai_index

CARRIERS = ('input-host-xbox-sdk.log.1', 'input-host-xbox-sdk.log')
INDEX = 'ai-log-index.jsonl'
MAX_MEMBER = 8 * 1024 * 1024  # Current writer rotates at 4 MiB; allow older logs.
MAX_INDEX = 16 * 1024 * 1024
MAX_ROW = 1024
SCOPED = {'registered-service', 'disk-image', 'service-enum'}
INDEXED = {'registered-binding', 'registered-driver'}
SNAPSHOT = re.compile(r'snapshot=(begin|end) (?:index=([0-7]) )?([^\r\n]+)\Z')
SCOPE_FIELDS = ('scopeId', 'scopeHostPid', 'hostInstanceId', 'runId', 'epoch', 'command', 'persona')
REF = 'sameAsBegin=1'
RELEASE_ENCODING = 'release-detail-zlib-v1:'
MAX_RELEASE_DETAIL = 4096
MAX_COVERAGE_SCOPES = 4096
SCOPE_END = re.compile(
    r'attemptedBeforeEnd=([0-9]{1,10}) writtenBeforeEnd=([0-9]{1,10}) '
    r'progress=([0-9]{1,10}) stages=([0-9]{1,10}) dropped=([0-9]{1,10}) '
    r'textTruncated=([0-9]{1,10}) logErrorsBeforeEnd=([0-9]{1,10}) '
    r'elapsedMs=[0-9]{1,18}\.[0-9];observations-not-availability-or-loaded-memory-image-proof')


def scope_key(row):
    scope, pid, host, run, epoch, command, persona = (row.get(k) for k in SCOPE_FIELDS)
    if (not isinstance(scope, str) or not re.fullmatch(r'[0-9a-f]{32}', scope)
            or type(pid) is not int or not 0 < pid < 1 << 32
            or type(epoch) is not int or not 0 <= epoch < 1 << 64 or persona != 'steamdeck'
            or not all(isinstance(v, str) and v.strip() and len(v) <= lim
                       for v, lim in ((host, 64), (run, 64), (command, 48)))):
        return None
    return scope, pid, host, run, epoch, command, persona


def setup_rows(members):
    # Scope rows are append ordered within a file. The single retained .1 is the
    # older rotation. Packing order and the wall-clock timestamp are not used.
    for rank, name in enumerate(CARRIERS):
        if name not in members:
            continue
        for number, line in enumerate(io.BytesIO(members[name]), 1):
            if len(line) > MAX_ROW or not line.endswith(b'\n'):
                continue
            try:
                row = evidence_json_loads(line)
            except (ValueError, UnicodeError):
                continue
            if not isinstance(row, dict) or row.get('kind') != 'usbip-setup-evidence':
                continue
            yield row, {'entry': name, 'line': number, 't': row.get('t')}, (rank, number)


def snapshot_records(members):
    for row, pos, order in setup_rows(members):
        phase, data = row.get('phase'), row.get('data')
        if not isinstance(phase, str) or phase not in SCOPED | INDEXED or not isinstance(data, dict):
            continue
        detail = data.get('detail')
        match = SNAPSHOT.fullmatch(detail) if isinstance(detail, str) else None
        if not match:
            continue
        when, index, payload = match.groups()
        scope = scope_key(row)
        shape_ok = (index is not None) == (phase in INDEXED)
        key = (*scope, phase, index) if scope and shape_ok else None
        complete = data.get('textTruncated') is False and 'available' in data and data['available'] is None
        yield when, key, phase, index, payload, complete, pos, order


def decode_release_detail(detail):
    packed = base64.b64decode(detail[len(RELEASE_ENCODING):], validate=True)
    decoder = zlib.decompressobj()
    raw = decoder.decompress(packed, MAX_RELEASE_DETAIL + 1)
    if not 0 < len(raw) <= MAX_RELEASE_DETAIL or not decoder.eof or decoder.unconsumed_tail or decoder.unused_data:
        raise ValueError('incomplete, oversized or trailing release detail')
    return raw.decode('utf-8', errors='strict')


def release_details(members):
    for row, pos, _ in setup_rows(members):
        if row.get('phase') != 'release-begin' or row.get('command') != 'RELEASE_LOCAL':
            continue
        result = {'position': pos, 'status': 'invalid', 'detail': None}
        scope, data = scope_key(row), row.get('data')
        if scope and isinstance(data, dict) and 'available' in data and data['available'] is None:
            result['scope'] = dict(zip(SCOPE_FIELDS, scope))
            detail, clipped = data.get('detail'), data.get('textTruncated')
            if isinstance(detail, str) and type(clipped) is bool:
                if detail.startswith(RELEASE_ENCODING):
                    if not clipped:
                        try:
                            result.update(status='decoded', detail=decode_release_detail(detail))
                        except (ValueError, zlib.error):
                            pass
                else:
                    result.update(status='truncated' if clipped else 'plain', detail=detail)
        yield result


def scope_end_counters(data):
    if (not isinstance(data, dict) or data.get('textTruncated') is not False or
            'available' not in data or data['available'] is not None):
        return None
    detail = data.get('detail')
    match = SCOPE_END.fullmatch(detail) if isinstance(detail, str) else None
    if not match:
        return None
    attempted, written, progress, stages, dropped, truncated, errors = map(int, match.groups())
    if (not (1 <= attempted <= 128 and written + errors == attempted and truncated <= attempted and
             progress <= 64 and stages <= 22 and progress + stages <= attempted and dropped < 1 << 31)):
        return None
    return dict(attemptedBeforeEnd=attempted, writtenBeforeEnd=written,
                dropped=dropped, textTruncated=truncated, logErrorsBeforeEnd=errors)


def scope_coverage(members):
    """Compare one writer's pre-end count with retained, bound rows in append order."""
    groups = {}
    total = unbound = 0
    for row, pos, _ in setup_rows(members):
        total += 1
        key = scope_key(row)
        if key is None or not isinstance(row.get('phase'), str) or not row['phase']:
            unbound += 1
            continue
        if key not in groups:
            if len(groups) == MAX_COVERAGE_SCOPES:
                return {'schemaVersion': 1, 'state': 'scope-limit-exceeded', 'maxScopes': MAX_COVERAGE_SCOPES}
            groups[key] = {'rows': 0, 'begins': 0, 'ends': 0, 'firstBegin': None, 'firstEnd': None,
                           'endPosition': None, 'endCounters': None}
        group = groups[key]
        group['rows'] += 1
        if row['phase'] == 'scope-begin':
            group['begins'] += 1
            if group['begins'] == 1:
                group['firstBegin'] = group['rows']
        elif row['phase'] == 'scope-end':
            group['ends'] += 1
            if group['ends'] == 1:
                group.update(firstEnd=group['rows'], endPosition=pos, endCounters=scope_end_counters(row.get('data')))

    commands = {}
    for key, group in groups.items():
        command = key[5] if key[5] in ('PREPARE_TARGET', 'RELEASE_LOCAL') else 'other'
        view = commands.setdefault(command, {'scopes': 0, 'boundaries': collections.Counter(),
                                            'retainedCount': collections.Counter(),
                                            'endCounterPositive': collections.Counter(), 'firstWitness': {}})
        view['scopes'] += 1
        begins, ends = group['begins'], group['ends']
        counters = group['endCounters']
        observed = None
        if begins > 1 or ends > 1:
            boundary, state = 'duplicate-boundary', 'not-comparable'
        elif (begins and group['firstBegin'] != 1) or (ends and group['firstEnd'] != group['rows']):
            boundary, state = 'boundary-out-of-order', 'not-comparable'
        else:
            boundary = ('paired' if ends else 'begin-only') if begins else ('end-only' if ends else 'body-only')
            if not ends:
                state = 'no-end-retained'
            elif counters is None:
                state = 'end-counters-unknown'
            else:
                observed = group['rows'] - 1
                reported = counters['writtenBeforeEnd']
                state = ('same-count' if observed == reported else
                         'fewer-retained-rows' if observed < reported else 'more-retained-rows')
                for name in ('dropped', 'textTruncated', 'logErrorsBeforeEnd'):
                    if counters[name] > 0:
                        view['endCounterPositive'][name] += 1
        view['boundaries'][boundary] += 1
        view['retainedCount'][state] += 1
        if ends and (boundary != 'paired' or state != 'same-count'):
            view['firstWitness'].setdefault(f'{boundary}:{state}', {
                'end': group['endPosition'], 'retainedBeforeEnd': observed,
                'reportedWrittenBeforeEnd': counters['writtenBeforeEnd'] if observed is not None else None,
            })
    return {'schemaVersion': 1, 'state': 'summarized', 'maxScopes': MAX_COVERAGE_SCOPES,
            'parsedSetupRows': total, 'unboundOrInvalidPhaseRows': unbound, 'commands': commands,
            'scope': 'retained parsed rows; count agreement is not complete history, delivery or operation success'}


def successful_begin(phase, payload):
    if not payload.startswith('status=observed '):
        return False
    if phase == 'service-enum':
        return bool(re.fullmatch(r'status=observed registeredCount=[0-8] limit=8 omitted=0', payload))
    if phase == 'registered-binding':
        return bool(re.match(r'status=observed locateResult=0 statusResult=0 cmStatus=[0-9]+ started=true problem=0 ', payload))
    return True


def resolve_references(members):
    starts, end_counts, refs = collections.defaultdict(list), collections.Counter(), []
    for when, key, phase, index, payload, complete, pos, order in snapshot_records(members):
        if when == 'begin' and key:
            starts[key].append((payload, complete, pos, order))
        elif when == 'end':
            if key:
                end_counts[key] += 1
            if payload == REF:
                refs.append((key, phase, index, complete, pos, order))
    for key, phase, index, complete, pos, order in refs:
        result = {'end': pos, 'phase': phase, 'index': int(index) if index is not None else None,
                  'status': 'UNKNOWN', 'begin': None, 'reconstructedDetail': None}
        candidates = starts.get(key, ())
        if not complete or not key:
            reason = 'invalid-or-truncated-reference'
        elif len(candidates) > 1 or end_counts[key] != 1:
            reason = 'ambiguous-snapshot-rows'
        elif not candidates:
            reason = 'begin-not-retained'
        else:
            payload, begin_complete, begin_pos, begin_order = candidates[0]
            if not begin_complete or not successful_begin(phase, payload):
                reason = 'begin-not-complete-success'
            elif begin_order >= order:
                reason = 'begin-not-prior'
            else:
                prefix = 'snapshot=end ' + (f'index={index} ' if index is not None else '')
                result.update(status='RESOLVED', begin=begin_pos, reconstructedDetail=prefix + payload)
                reason = None
        result['reason'] = reason
        yield result


def read_members(archive):
    wanted = {name: [] for name in (*CARRIERS, INDEX)}
    infos = archive.infolist()
    if len(infos) > MAX_MEMBERS:
        raise ValueError('member-count budget exceeded')
    for entry in infos:
        if entry.filename in wanted:
            wanted[entry.filename].append(entry)
    if any(len(entries) > 1 for entries in wanted.values()):
        raise ValueError('duplicate USBIP carrier or export index')
    expected = None
    if wanted[INDEX]:
        info = wanted[INDEX][0]
        if info.file_size > MAX_INDEX:
            raise ValueError('export index exceeds reader budget')
        expected = validated_ai_index(archive.read(info))
    members = {}
    for name in CARRIERS:
        if not wanted[name]:
            continue
        info = wanted[name][0]
        if info.file_size > MAX_MEMBER:
            raise ValueError('USBIP carrier exceeds reader budget')
        data = archive.read(info)  # zipfile verifies the member CRC.
        if expected is not None:
            record = expected.get((name, 0))
            if not record or record['bytes'] != len(data) or record['sha256'] != hashlib.sha256(data).hexdigest():
                raise ValueError('USBIP carrier size/SHA does not match export index')
        members[name] = data
    return members


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('--release-details', action='store_true', help='Include cached PnP descriptions from USBIP release receipts')
    parser.add_argument('--scope-coverage', action='store_true', help='Compare retained USBIP setup rows with scope-end writer counters')
    args = parser.parse_args()
    try:
        with zipfile.ZipFile(args.archive) as archive:
            members = read_members(archive)
        result = {'members': list(members), 'references': list(resolve_references(members)),
                  'scope': 'retained snapshot details only; not driver or availability proof'}
        if args.release_details:
            result['releaseDetails'] = list(release_details(members))
        if args.scope_coverage:
            result['scopeCoverage'] = scope_coverage(members)
        print(json.dumps(result, ensure_ascii=True))
    except (ValueError, zipfile.BadZipFile, OSError) as error:
        parser.exit(2, f'{error}\n')


if __name__ == '__main__':
    main()
