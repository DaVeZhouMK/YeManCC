"""Compare desktop inventories and verify a consecutive ordinary export pair."""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path, PureWindowsPath
import re
import zipfile
import zlib

from ai_log_reader import evidence_json_loads
from log_lane_audit import (AUDIT_MANIFEST_NAME, MAX_AUDIT_RECEIPT_BYTES,
                            ORDINARY_EXPORT_OPERATION_FIELDS, CrossArchiveExportTerminals,
                            archive_attempt_timing_record, cooldown_query_receipt_comparison,
                            export_operation_record, ordinary_export_terminal_record,
                            ordinary_export_transaction_evidence, windows_query_cohort_record,
                            windows_query_scheduling_cohort_record)


STAGES = ('before', 'after-first', 'after-second')
AVAILABILITY = 'log-export-availability.json'
NATIVE_BUILD_PREFIX = 'YMCC-NATIVE-BUILD:v1:'
NATIVE_BUILD_SCOPE = 'executing-main-compiled-marker-at-export;not-disk-identity-or-historical-writers'
NATIVE_BUILD_ID = re.compile(r'[0-9a-f]{64}')
EXPORT_ZIP_NAME = re.compile(r'YeManCC-logs-[0-9]{8}-[0-9]{6}-([0-9]+-[0-9]+-[0-9]+)\.zip', re.IGNORECASE | re.ASCII)
LOCAL_TERMINAL_SCOPE = 'bounded-local-lifecycle-observation;not-page-timing-or-desktop-proof'
STAGING_SCOPE = 'after-second; bounded names; metadata only; no history or holder identity'


def utc_time(value):
    if not isinstance(value, str) or not re.fullmatch(r'[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{7}Z', value):
        raise ValueError('expected a UTC timestamp')
    datetime.fromisoformat(value)
    return value  # The fixed UTC format preserves the seventh fractional digit.


def load_snapshot(path, stage, raw=None):
    value = evidence_json_loads(path.read_bytes() if raw is None else raw)
    keys = {'schema', 'stage', 'directory', 'scanStartedUtc', 'scanEndedUtc', 'files'}
    if (not isinstance(value, dict) or value.keys() != keys or type(value['schema']) is not int or
            value['schema'] != 1 or value['stage'] != stage or not isinstance(value['directory'], str) or
            not isinstance(value['files'], list)):
        raise ValueError(f'invalid snapshot: {path.name}')
    root = PureWindowsPath(value['directory'])
    started, ended = (utc_time(value[key]) for key in ('scanStartedUtc', 'scanEndedUtc'))
    if not root.is_absolute() or ended < started:
        raise ValueError(f'invalid directory or scan interval: {path.name}')
    files = {}
    for row in value['files']:
        if (not isinstance(row, dict) or row.keys() != {'name', 'kind', 'bytes', 'lastWriteUtc'} or
                not isinstance(row['name'], str) or not row['name'].lower().startswith('yemancc-logs-') or
                any(c in row['name'] for c in ('/', '\\', ':', '\0')) or
                type(row['bytes']) is not int or row['bytes'] < 0):
            raise ValueError(f'invalid inventory row: {path.name}')
        name = row['name'].casefold()
        kind = ('attempt' if '.zip.attempt-' in name else
                'zip' if name.endswith('.zip') else 'other-export-name')
        if row['kind'] != kind or name in files:
            raise ValueError(f'incorrect kind or repeated name: {path.name}')
        utc_time(row['lastWriteUtc'])
        files[name] = row
    return root, started, ended, files


def running_build_evidence(availability, operation, expected_build_id):
    if operation is None:
        return dict(state='operation-identity-unverified')
    if 'runningNativeBuild' not in availability:
        return dict(state='not-reported')
    value = availability['runningNativeBuild']
    if (not isinstance(value, dict) or type(value.get('schemaVersion')) is not int or
            value['schemaVersion'] != 1 or value.get('evidenceScope') != NATIVE_BUILD_SCOPE):
        return dict(state='unsupported-report')
    if value.get('markerKnown') is not True:
        return dict(state='marker-not-known')
    marker = value.get('compiledMarker')
    if not isinstance(marker, str) or not marker.startswith(NATIVE_BUILD_PREFIX):
        return dict(state='invalid-marker')
    build_id = marker[len(NATIVE_BUILD_PREFIX):]
    if not NATIVE_BUILD_ID.fullmatch(build_id):
        return dict(state='invalid-marker')
    if (type(value.get('writerPid')) is not int or value['writerPid'] != operation['writerPid'] or
            value.get('writerProcessCreation100ns') != operation['writerProcessCreation100ns']):
        return dict(state='different-operation-writer', reportedBuildId=build_id)
    return dict(state='matches-candidate' if build_id == expected_build_id else 'different-build',
                reportedBuildId=build_id)


def read_operation(archive, expected_build_id=None, pair_receipts=False):
    receipts = [item for item in archive.infolist() if item.filename == AVAILABILITY]
    if len(receipts) != 1:
        return dict(state='missing-receipt' if not receipts else 'ambiguous-receipt', operation=None)
    receipt = receipts[0]
    if receipt.file_size > MAX_AUDIT_RECEIPT_BYTES:
        return dict(state='receipt-over-byte-limit', operation=None)
    try:
        raw = archive.read(receipt)
        availability = evidence_json_loads(raw)
    except (ValueError, RecursionError):
        return dict(state='invalid-receipt', operation=None)
    parsed = export_operation_record(availability)
    state, operation = parsed['state'], None
    if state == 'reported-process-operation':
        if availability['exportOperation'].keys() == ORDINARY_EXPORT_OPERATION_FIELDS:
            operation = parsed['reported']
        else:
            state = 'unsupported-operation-fields'
    result = dict(state=state, operation=operation, receiptSha256=hashlib.sha256(raw).hexdigest())
    if expected_build_id is not None:
        result['runningNativeBuild'] = running_build_evidence(availability, operation, expected_build_id)
    if pair_receipts and operation is not None:
        transaction = ordinary_export_transaction_evidence(availability, {'status': 'selected'}, ())
        carry = transaction['precedingOperationCarry']
        result['receiptDetails'] = dict(
            windowsQuery=windows_query_cohort_record(transaction),
            windowsScheduling=windows_query_scheduling_cohort_record(transaction),
            precedingTerminal=dict(state=carry['state'], operation=carry['operation'],
                                   native=carry['terminal']['reported'] if carry['terminal'] else None,
                                   bodyResultOk=carry['bodyResultOk'], rendererReply=carry.get('rendererReply')))
    return result


def first_bad_zip_member(archive):
    # Open the member itself so a repeated name cannot hide an earlier entry.
    for member in archive.infolist():
        try:
            with archive.open(member) as stream:
                while stream.read(1024 * 1024):
                    pass
        except (zipfile.BadZipFile, OSError, RuntimeError, zlib.error):
            return member.filename
    return None


def read_export_mode(archive):
    manifests = [item for item in archive.infolist() if item.filename == AUDIT_MANIFEST_NAME]
    if len(manifests) != 1:
        return dict(state='ambiguous-manifest' if manifests else 'missing-manifest')
    if manifests[0].file_size > MAX_AUDIT_RECEIPT_BYTES:
        return dict(state='manifest-over-byte-limit')
    try:
        manifest = archive.read(manifests[0]).decode('utf-8-sig')
    except UnicodeError:
        return dict(state='invalid-manifest-encoding')
    modes = re.findall(r'^logExportMode=(.*)$', manifest, re.M)
    if len(modes) != 1:
        return dict(state='ambiguous-mode' if modes else 'missing-mode')
    mode = modes[0].strip()
    if mode not in ('full', 'quick'):
        return dict(state='unsupported-mode')
    return dict(state='reported', mode=mode)


def check_zip(path, expected_build_id=None, pair_receipts=False):
    operation = dict(state='archive-unreadable', operation=None)
    mode = dict(state='archive-unreadable')
    with path.open('rb') as stream:
        size = stream.seek(0, 2)
        stream.seek(0)
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
        stream.seek(0)
        try:
            with zipfile.ZipFile(stream) as archive:
                bad = first_bad_zip_member(archive)
                read_ok = any(not member.is_dir() for member in archive.filelist) and bad is None
                if read_ok:
                    operation = read_operation(archive, expected_build_id, pair_receipts)
                    if expected_build_id is not None:
                        mode = read_export_mode(archive)
        except (zipfile.BadZipFile, OSError, RuntimeError, zlib.error):
            bad, read_ok = None, False
    result = dict(name=path.name, bytes=size, sha256=digest, crcPassed=read_ok, firstBadMember=bad,
                  operationEvidence=operation)
    if expected_build_id is not None:
        result['manifestExportMode'] = mode
    return result


def compare_operations(first, second):
    first, second = (item['operationEvidence']['operation'] for item in (first, second))
    if first is None or second is None:
        state = 'operation-identity-unverified'
    elif first == second:
        state = 'same-operation'
    elif any(first[key] != second[key] for key in ('writerPid', 'writerProcessCreation100ns')):
        state = 'different-writer-lifetimes'
    elif int(second['ordinal']) != int(first['ordinal']) + 1:
        state = 'operations-not-consecutive'
    elif int(second['startedTickMs']) <= int(first['startedTickMs']):
        state = 'operation-start-order-conflict'
    else:
        state = 'consecutive-reported-operations'
    return dict(state=state, scope='selected-in-zip-operation-identity;not-running-build-or-ipc-proof')


def deadline_pair_evidence(first, second, first_query, second_query):
    result = dict(state='first-deadline-unverified', secondQueryAttempts=second_query['queryAttempts'],
                  windowMs=None, secondStartAfterFirstQueryMs=None)
    policy = first_query['deadlinePolicy']
    if first_query['state'] != 'capture-deadline':
        if first_query['state'] != 'unclassified':
            result['state'] = 'first-query-did-not-report-deadline'
        return result
    if policy is None:
        return dict(result, state='first-deadline-policy-unverified')
    completed = int(policy['completedTickMs'])
    delay = int(second['startedTickMs']) - completed
    result.update(windowMs=policy['cooldownMs'], secondStartAfterFirstQueryMs=delay)
    if completed < int(first['startedTickMs']) or delay < 0:
        return dict(result, state='inconsistent-completion-tick')
    if second_query['state'] == 'cooldown-no-current-query':
        recent = second_query['previousDeadline']
        if recent.get('originalExportId') != first['id']:
            return dict(result, state='cooldown-does-not-reference-first')
        required = {'powerGeneration', 'completedTickMs', 'cooldownMs', 'deadlineStreak', 'streakWindowMs'}
        if not required <= recent.keys() or not first_query['currentQuery'].keys() <= recent['query'].keys():
            return dict(result, state='cooldown-origin-fields-incomplete')
        origin = dict(observation=first_query, conflictingReceipts=False)
        comparison = cooldown_query_receipt_comparison(
            dict(operation=second, observation=second_query), {CrossArchiveExportTerminals.key(first): origin})
        result['originComparison'] = comparison
        if comparison['state'] != 'reported-origin-fields-agree':
            return dict(result, state='cooldown-origin-conflict')
        if recent['ageMs'] < delay:
            return dict(result, state='cooldown-age-conflicts-with-operation-start')
        return dict(result, state='reported-same-generation-cooldown-hit', reportedAgeMs=recent['ageMs'])
    if delay >= policy['cooldownMs']:
        return dict(result, state='second-started-after-window')
    if second_query['deadlinePolicy'] and second_query['deadlinePolicy']['powerGeneration'] != policy['powerGeneration']:
        return dict(result, state='second-query-reports-different-generation')
    # The start is a lower bound on when the second export checks the deadline.
    return dict(result, state='cooldown-hit-not-observed')


def paired_receipt_evidence(first, second, operations):
    scope = 'selected-zip-receipts-only;separate-from-desktop-delivery-and-actual-page-timing'
    if operations['state'] != 'consecutive-reported-operations':
        return dict(scope=scope, state='operation-pair-unverified')
    left, right = (item['operationEvidence'] for item in (first, second))
    a, b = left['receiptDetails'], right['receiptDetails']
    carry = dict(b['precedingTerminal'])
    if carry['state'] == 'reported-in-memory-preceding-terminal' and carry['operation'] != left['operation']:
        carry = dict(state='preceding-terminal-references-different-operation')
    return dict(scope=scope, state='consecutive-receipts-inspected', firstTerminalFromSecondReceipt=carry,
                deadline=deadline_pair_evidence(left['operation'], right['operation'],
                                                a['windowsQuery'], b['windowsQuery']))


def check_archive_name(supplied):
    name = supplied['name']
    operation = supplied['operationEvidence']['operation']
    match = EXPORT_ZIP_NAME.fullmatch(name)
    if operation is None:
        state = 'operation-identity-unverified'
    elif match is None:
        state = 'noncanonical-export-name'
    elif match[1] != operation['id']:
        state = 'different-operation-id'
    else:
        state = 'matches-operation-id'
    return dict(name=name, state=state, operationId=operation['id'] if operation else None,
                filenameOperationId=match[1] if match else None)


def compare_stage(stage, before, after, supplied):
    added = [after[key] for key in sorted(after.keys() - before.keys())]
    removed = [before[key] for key in sorted(before.keys() - after.keys())]
    changed = [dict(before=before[key], after=after[key]) for key in sorted(before.keys() & after.keys())
               if before[key] != after[key]]
    new_zip_matches = (len(added) == 1 and added[0]['kind'] == 'zip' and
                       added[0]['name'].casefold() == supplied['name'].casefold() and
                       added[0]['bytes'] == supplied['bytes'])
    attempt_changes = [row['name'] for row in added if row['kind'] == 'attempt']
    attempt_changes.extend(row['after']['name'] for row in changed if row['after']['kind'] == 'attempt')
    return dict(stage=stage, conforms=new_zip_matches and supplied['crcPassed'] and not removed and not changed,
                added=added, removed=removed, changed=changed, newOrChangedAttemptNames=attempt_changes,
                suppliedZipMatchesOnlyAddedNameAndSize=new_zip_matches, suppliedZip=supplied)


def desktop_hash_evidence(path, after_second, steps):
    value = evidence_json_loads(path.read_bytes())
    if (not isinstance(value, dict) or value.keys() != {'schema', 'directory', 'captureStartedUtc', 'files'} or
            type(value['schema']) is not int or value['schema'] != 1 or not isinstance(value['directory'], str) or
            not isinstance(value['files'], list) or len(value['files']) != 2):
        raise ValueError('expected a desktop hash report with two files')
    if (PureWindowsPath(value['directory']) != after_second[0] or
            utc_time(value['captureStartedUtc']) < after_second[2]):
        raise ValueError('desktop hashes must be observed after the last scan in the same reported directory')
    rows = {}
    for row in value['files']:
        if (not isinstance(row, dict) or row.keys() != {'name', 'bytes', 'lastWriteUtc', 'sha256'} or
                not isinstance(row['name'], str) or type(row['bytes']) is not int or row['bytes'] < 0 or
                not isinstance(row['sha256'], str) or not re.fullmatch(r'[0-9a-fA-F]{64}', row['sha256'])):
            raise ValueError('invalid desktop hash row')
        utc_time(row['lastWriteUtc'])
        name = row['name'].casefold()
        if name in rows:
            raise ValueError('repeated desktop hash name')
        rows[name] = row

    comparisons = []
    for step in steps:
        supplied = step['suppliedZip']
        name = supplied['name'].casefold()
        recorded, last = rows.get(name), after_second[3].get(name)
        if not recorded or not last or last['kind'] != 'zip':
            state = 'desktop-file-not-matched'
        elif any(recorded[key] != last[key] for key in ('bytes', 'lastWriteUtc')):
            state = 'desktop-metadata-differs-from-last-scan'
        else:
            state = 'matches-reported-desktop-hash' if supplied['sha256'] == recorded['sha256'].lower() else 'hash-mismatch'
        comparisons.append(dict(name=supplied['name'], state=state,
                                reportedSha256=recorded['sha256'].lower() if recorded else None))
    return dict(scope='reported-post-second-desktop-hash;not-durability-or-content-at-earlier-scans',
                matchesBoth=all(row['state'] == 'matches-reported-desktop-hash' for row in comparisons),
                files=comparisons)


def staging_scan_time(value):
    if isinstance(value, str) and value.endswith('+00:00'):
        value = value[:-6] + 'Z'
    return utc_time(value)


def staging_metadata(value):
    if not isinstance(value, dict):
        raise ValueError('invalid staging metadata')
    state = value.get('state')
    if state == 'regular-file-metadata':
        if (value.keys() == {'state', 'bytes', 'lastWriteUtc'} and
                type(value['bytes']) is int and value['bytes'] >= 0):
            utc_time(value['lastWriteUtc'])
            return state
    elif state == 'metadata-query-failed':
        if value.keys() == {'state', 'hresult'} and type(value['hresult']) is int:
            return state
    elif state in ('directory', 'not-found', 'reparse-point-not-followed') and value.keys() == {'state'}:
        return state
    raise ValueError('invalid staging metadata')


def desktop_staging_evidence(path, after_second, after_hash, steps):
    with path.open('rb') as stream:
        raw = stream.read(MAX_AUDIT_RECEIPT_BYTES + 1)
    if len(raw) > MAX_AUDIT_RECEIPT_BYTES:
        raise ValueError('desktop staging report is too large')
    value = evidence_json_loads(raw)
    keys = {'schema', 'scope', 'directory', 'afterSecondInventorySha256', 'scanStartedUtc', 'scanEndedUtc', 'operations'}
    if (not isinstance(value, dict) or value.keys() != keys or type(value['schema']) is not int or
            value['schema'] != 1 or value['scope'] != STAGING_SCOPE or
            not isinstance(value['directory'], str) or PureWindowsPath(value['directory']) != after_second[0] or
            value['afterSecondInventorySha256'] != after_hash or
            not isinstance(value['operations'], list) or not 1 <= len(value['operations']) <= 2):
        raise ValueError('desktop staging report does not bind to the last desktop inventory')
    started, ended = (staging_scan_time(value[key]) for key in ('scanStartedUtc', 'scanEndedUtc'))
    if not after_second[2] <= started <= ended:
        raise ValueError('desktop staging scan must follow the last desktop inventory')
    expected = {step['suppliedZip']['name'].casefold(): step['suppliedZip'] for step in steps}
    operations = {}
    for row in value['operations']:
        if (not isinstance(row, dict) or row.keys() != {'zipName', 'scanState', 'scanErrorHresult', 'entries'} or
                not isinstance(row['zipName'], str) or not isinstance(row['entries'], list) or
                row['scanState'] not in ('complete', 'enumeration-failed') or
                (row['scanErrorHresult'] is not None if row['scanState'] == 'complete'
                 else type(row['scanErrorHresult']) is not int)):
            raise ValueError('invalid desktop staging operation')
        name = row['zipName'].casefold()
        supplied = expected.get(name)
        if name in operations or not supplied or check_archive_name(supplied)['state'] != 'matches-operation-id':
            raise ValueError('desktop staging operation does not match a supplied original ZIP')
        op = supplied['operationEvidence']['operation']
        pattern = re.compile(re.escape(name) + rf"\.staging-{op['writerPid']}-([0-9]{{1,20}})-[0-9]{{1,20}}-([12])")
        seen = set()
        temporary_files = 0
        incomplete = row['scanState'] != 'complete'
        for entry in row['entries']:
            if (not isinstance(entry, dict) or entry.keys() != {'name', 'attempt', 'entry', 'temporary'} or
                    not isinstance(entry['name'], str) or type(entry['attempt']) is not int):
                raise ValueError('invalid desktop staging entry')
            leaf = entry['name'].casefold()
            match = pattern.fullmatch(leaf)
            if (not match or leaf in seen or int(match[1]) < int(op['startedTickMs']) or int(match[2]) != entry['attempt']):
                raise ValueError('desktop staging entry does not match the operation or attempt')
            seen.add(leaf)
            entry_state = staging_metadata(entry['entry'])
            if entry_state == 'directory':
                temporary = staging_metadata(entry['temporary'])
                temporary_files += temporary == 'regular-file-metadata'
                incomplete |= temporary not in ('regular-file-metadata', 'not-found')
            else:
                if entry['temporary'] is not None:
                    raise ValueError('temporary metadata requires a staging directory')
                incomplete = True
        operations[name] = dict(row, temporaryFilesObserved=temporary_files, observationComplete=not incomplete)
    return dict(scope=STAGING_SCOPE, scanStartedUtc=started, scanEndedUtc=ended,
                operations=[operations.get(name, dict(zipName=expected[name]['name'], scanState='not-requested'))
                            for name in expected])


def local_terminal_evidence(path, supplied, last_scan_ended):
    with path.open('rb') as stream:
        raw = stream.read(MAX_AUDIT_RECEIPT_BYTES + 1)
    if len(raw) > MAX_AUDIT_RECEIPT_BYTES:
        raise ValueError('local terminal receipt is too large')
    value = evidence_json_loads(raw)
    if (not isinstance(value, dict) or type(value.get('schema')) is not int or value['schema'] != 1 or
            value.get('scope') != LOCAL_TERMINAL_SCOPE or not isinstance(value.get('archiveName'), str) or
            not isinstance(value.get('archiveSha256'), str) or not NATIVE_BUILD_ID.fullmatch(value['archiveSha256'])):
        raise ValueError('invalid local terminal receipt')
    view = dict(scope=LOCAL_TERMINAL_SCOPE, reportsSuccess=False)
    if (value['archiveSha256'] != supplied['sha256'] or
            value['archiveName'].casefold() != supplied['name'].casefold()):
        return dict(view, state='different-archive')
    if utc_time(value.get('capturedUtc')) < last_scan_ended:
        return dict(view, state='captured-before-last-scan-ended')
    operation = supplied['operationEvidence']['operation']

    def matches_operation(carrier):
        parsed = export_operation_record(carrier)
        return (operation is not None and parsed['state'] == 'reported-process-operation' and
                parsed['reported'] == operation and carrier['exportOperation'].keys() == ORDINARY_EXPORT_OPERATION_FIELDS)

    if not matches_operation(value):
        return dict(view, state='different-operation')
    count, terminal = value.get('matches'), value.get('terminal')
    if type(count) is not int or count < 0:
        raise ValueError('invalid local terminal count')
    state = 'terminal-not-observed' if count == 0 else 'one-matching-terminal' if count == 1 else 'ambiguous-terminal'
    if value.get('state') != state or (count != 1 and terminal is not None):
        raise ValueError('inconsistent local terminal receipt')
    if count != 1:
        return dict(view, state=state)
    if (not isinstance(terminal, dict) or terminal.get('file') not in ('native-lifecycle.log', 'native-lifecycle.log.1') or
            not isinstance(terminal.get('jsonLine'), str)):
        raise ValueError('invalid local terminal line')
    line = terminal['jsonLine'].encode('utf-8')
    if len(line) > 512 * 1024 or b'\n' in line or b'\r' in line:
        raise ValueError('invalid local terminal line extent')
    row = evidence_json_loads(line)
    if (not isinstance(row, dict) or row.get('event') != 'log-export-result' or not matches_operation(row) or
            type(row.get('pid')) is not int or row['pid'] != operation['writerPid'] or
            row.get('writerProcessCreation100ns') != operation['writerProcessCreation100ns']):
        return dict(view, state='different-terminal-writer-or-operation')
    result = ordinary_export_terminal_record(row)
    reported = result['reported']
    if (result['fieldIssues'] or result['terminalStageState'] != 'coherent-reported-stage' or
            reported.get('exportMode') != 'full' or 'ok' not in reported or 'exportElapsedMs' not in reported):
        return dict(view, state='invalid-terminal-fields', reported=reported)
    timing = archive_attempt_timing_record(row)
    success = (reported['ok'] is True and reported.get('terminalStage') == 'archive-return' and
               reported.get('attempts') in (1, 2) and
               0 <= reported.get('archiveElapsedMs', -1) <= reported['exportElapsedMs'])
    return dict(view, state='reported-success' if success else 'terminal-does-not-report-archive-success',
                reportsSuccess=success, reported=reported, sourceFile=terminal['file'],
                completedPhaseElapsedMs=result['phaseTiming']['elapsedMs'], archiveWallBreakdown=timing.get('wallBreakdown'))


def audit(directory, first, second, expected_build_id=None, desktop_hashes=None, second_native_terminal=None,
          pair_receipts=False, desktop_staging=None):
    after_raw = (directory / 'desktop-after-second.json').read_bytes() if desktop_staging is not None else None
    before, after_first, after_second = [load_snapshot(directory / f'desktop-{stage}.json', stage,
                                                       after_raw if stage == 'after-second' else None) for stage in STAGES]
    if not (before[0] == after_first[0] == after_second[0] and
            before[2] <= after_first[1] and after_first[2] <= after_second[1]):
        raise ValueError('snapshots must be chronological and from the same reported directory')
    first_zip, second_zip = (check_zip(path, expected_build_id, pair_receipts) for path in (first, second))
    steps = [compare_stage('after-first', before[3], after_first[3], first_zip),
             compare_stage('after-second', after_first[3], after_second[3], second_zip)]
    operations = compare_operations(*(step['suppliedZip'] for step in steps))
    inventory_matches = all(step['conforms'] for step in steps)
    result = dict(scope='reported-name-size-and-time-inventories;supplied-zip-crc;not-desktop-byte-identity-or-durability',
                  conforms=inventory_matches and operations['state'] == 'consecutive-reported-operations',
                  inventoryMatches=inventory_matches, operationPair=operations, stages=steps)
    if pair_receipts:
        result['pairReceipts'] = paired_receipt_evidence(first_zip, second_zip, operations)
    if expected_build_id is not None:
        builds = [step['suppliedZip']['operationEvidence'].get('runningNativeBuild', {}) for step in steps]
        matches = all(build.get('state') == 'matches-candidate' for build in builds)
        result['candidateBuild'] = dict(expectedBuildId=expected_build_id, matchesBoth=matches,
                                        scope=NATIVE_BUILD_SCOPE)
        modes = [step['suppliedZip']['manifestExportMode'] for step in steps]
        full_modes = all(mode.get('mode') == 'full' for mode in modes)
        result['ordinaryExportMode'] = dict(expectedMode='full', matchesBoth=full_modes,
                                            scope='unique-export-manifest-mode-per-supplied-zip')
        names = [check_archive_name(step['suppliedZip']) for step in steps]
        names_match = all(name['state'] == 'matches-operation-id' for name in names)
        result['archiveNames'] = dict(matchesBoth=names_match, files=names,
                                      scope='generated-name-format-and-operation-suffix;not-wall-clock-or-byte-identity')
        result['conforms'] = result['conforms'] and matches and full_modes and names_match
    if desktop_hashes is not None:
        evidence = desktop_hash_evidence(desktop_hashes, after_second, steps)
        result['desktopHashes'] = evidence
        result['conforms'] = result['conforms'] and evidence['matchesBoth']
    if second_native_terminal is not None:
        evidence = local_terminal_evidence(second_native_terminal, steps[1]['suppliedZip'], after_second[2])
        result['secondNativeTerminal'] = evidence
        result['conforms'] = result['conforms'] and evidence['reportsSuccess']
    if desktop_staging is not None:
        result['desktopStaging'] = desktop_staging_evidence(desktop_staging, after_second,
                                                             hashlib.sha256(after_raw).hexdigest(), steps)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('inventory_directory', type=Path)
    parser.add_argument('first_zip', type=Path)
    parser.add_argument('second_zip', type=Path)
    parser.add_argument('--expected-native-build-id', type=str.lower,
                        help='require this Native build ID, full export mode, and matching generated ZIP names')
    parser.add_argument('--desktop-hashes', type=Path,
                        help='compare both supplied ZIPs with their post-second-scan desktop hashes')
    parser.add_argument('--second-native-terminal', type=Path,
                        help='verify a successful local terminal for the second ZIP')
    parser.add_argument('--desktop-staging', type=Path,
                        help='bind a post-second-scan staging report; does not change desktop delivery checks')
    parser.add_argument('--pair-receipts', action='store_true',
                        help='report first Native/renderer carry and cooldown evidence; does not change delivery checks')
    args = parser.parse_args()
    if args.expected_native_build_id is not None and not NATIVE_BUILD_ID.fullmatch(args.expected_native_build_id):
        parser.error('--expected-native-build-id requires the full 64-digit hex build ID')
    try:
        result = audit(args.inventory_directory, args.first_zip, args.second_zip, args.expected_native_build_id,
                       args.desktop_hashes, args.second_native_terminal, pair_receipts=args.pair_receipts,
                       desktop_staging=args.desktop_staging)
    except (OSError, ValueError, UnicodeError, RecursionError) as exc:
        parser.error(str(exc))
    print(json.dumps(result, indent=2, ensure_ascii=True))
    return 0 if result['conforms'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
