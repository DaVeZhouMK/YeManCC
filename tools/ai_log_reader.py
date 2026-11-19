"""AI-first, read-only log envelopes. Stdlib only; never executes evidence content.

python tools/ai_log_reader.py EXPORT.zip --output records.jsonl
python tools/ai_log_reader.py EXPORT.zip --index-only --output index.jsonl
The original archive is always authoritative. Packing order is not chronology.
Reserved root guide/index payloads are marked as metadata, not runtime events.
Reader wrapper fileId and an index payload's value.fileId are separate scopes.
"""
from __future__ import annotations
import argparse
import base64
import collections
import hashlib
import io
import json
import math
from pathlib import Path, PurePosixPath
import re
import zipfile
import zlib

ZIP_READ_ERRORS = (zipfile.BadZipFile, RuntimeError, OSError, EOFError, zlib.error)
ZIP_INTEGRITY_ERRORS = (zipfile.BadZipFile, zlib.error, EOFError)
try:
    from lzma import LZMAError
except ImportError:
    pass  # LZMA remains optional, as in zipfile itself.
else:
    ZIP_READ_ERRORS += (LZMAError,)
    ZIP_INTEGRITY_ERRORS += (LZMAError,)

COST_ARCHIVE = re.compile(r'input-host-cost\.log\.archive\.[0-9]{19}\.[0-9a-f]{24}\.zip\Z')
MAX_MEMBER = 512 * 1024 * 1024
MAX_TOTAL = 2 * 1024 * 1024 * 1024
MAX_MEMBERS = 16384
LINE_BYTES = 64 * 1024
MAX_JSON_CONTAINER_DEPTH = 128
BYTE_EXCERPTS = 8
BYTE_EXCERPT_BYTES = 256
ASCII_BYTE_RUN = re.compile(rb'[\x20-\x7e\t]{8,}')
JSONL = {
    'input-capture.jsonl', 'model-adaptation.log', 'virtual-adaptation.log', 'virtual-adaptation-session.jsonl', 'model-adaptation-session.jsonl', 'model-adaptation-signals-session.jsonl', 'native-lifecycle.log', 'native-detail.log', 'monitor-detail.log',
    'tdp.log', 'tdp-detail.log', 'frontend-errors.log', 'frontend-trace.log', 'frontend-trace-detail.log',
    'webview-failures.log', 'fan-api.log', 'fan-lifecycle.log', 'gyro-motion.log', 'gyro-motion-samples.log', 'virtual-gamepad.log',
    'focus-trace.log', 'hardware-coordinator.log', 'input-host-fatal.log', 'input-host-rejects.log', 'decky-sidebar.log',
    'input-host-cost.log', 'input-host-signal.log', 'input-host-report-chain.log', 'input-host-xbox-sdk.log',
    'recovery-service.log', 'steam-original-recovery.jsonl', 'fan-host-fault.log', 'yeman-fan-host-runtime.log', 'sleep-facts.log',
    'battery-charge-client.log', 'battery-charge-host.log', 'msi-controller-visibility.log', 'ai-log-index.jsonl',
}


class DuplicateJsonKeyError(ValueError):
    """Rejected decoded key only;no member value or ambiguous object survives."""
    def __init__(self,key):
        super().__init__('duplicate JSON object key')
        prefix=key[:128].encode('utf-8',errors='surrogatepass')
        retained=prefix[:128]
        self.key_evidence={'schemaVersion':1,'decodedKeyCharacters':len(key),
            'prefixHex':retained.hex(),'prefixBytes':len(retained),'maxPrefixBytes':128,
            'prefixComplete':len(key)<=128 and len(prefix)<=128,
            'encoding':'UTF-8-surrogatepass-of-decoded-key;not-original-JSON-spelling',
            'selection':'first-duplicate-rejected-by-bottom-up-object-hook;not-byte-order-or-all-duplicates',
            'minimumOccurrences':2,'objectPathKnown':False,'duplicateValuesRetained':False,'documentAccepted':False}


class JsonNestingLimitError(ValueError):
    def __init__(self, boundary):
        budget = boundary == 'container-depth-budget'
        super().__init__('JSON container depth budget exceeded; original retained' if budget else
                         'JSON nesting exceeds parser recursion capacity; original retained')
        self.nesting_evidence = {'boundary':boundary, 'maxProjectedContainerDepth':MAX_JSON_CONTAINER_DEPTH,
                                 'minObservedContainerDepth':MAX_JSON_CONTAINER_DEPTH+1 if budget else None}


def evidence_error_type(error):
    return 'ValueError' if isinstance(error,JsonNestingLimitError) else type(error).__name__


def json_rejection_evidence(error):
    if isinstance(error,DuplicateJsonKeyError):
        return {'duplicateKeyEvidence':error.key_evidence}
    if isinstance(error,JsonNestingLimitError):
        return {'jsonNestingEvidence':error.nesting_evidence}
    return {}


def unique_json_object(pairs):
    value = {}
    for key, item in pairs:
        if key in value:
            raise DuplicateJsonKeyError(key)
        value[key] = item
    return value


def finite_json_float(token):
    value = float(token)
    if not math.isfinite(value):
        raise ValueError('non-finite JSON number')
    return value


def reject_json_constant(token):
    raise ValueError('non-standard JSON number')


def evidence_json_loads(data):
    """Reject ambiguous keys/nonfinite values before projecting evidence.

    Ordinary payload failures retain their exact raw bytes. Index/volume
    validation uses its existing rejection policy; byte coverage is separate
    from whether retained JSON can be interpreted. At most 128 container
    levels are projected; deeper or interpreter-unparseable values stay raw.
    """
    try:
        value = json.loads(data, object_pairs_hook=unique_json_object,
                           parse_float=finite_json_float, parse_constant=reject_json_constant)
    except RecursionError as exc:
        raise JsonNestingLimitError('parser-recursion-capacity') from exc
    # Bound nesting independently of Python's interpreter recursion setting.
    # The iterator stack grows with depth, never with the width of an array.
    # This also prevents accepted data from later breaking JSON envelope output.
    frames = [(iter((value,)), 0)]
    while frames:
        children, depth = frames[-1]
        for child in children:
            if isinstance(child, dict):
                nested = iter(child.values())
            elif isinstance(child, list):
                nested = iter(child)
            else:
                continue
            if depth >= MAX_JSON_CONTAINER_DEPTH:
                raise JsonNestingLimitError('container-depth-budget')
            frames.append((nested, depth + 1))
            break
        else:
            frames.pop()
    return value


def canonical_name(name):
    name = name.rsplit('!', 1)[-1].rsplit('/', 1)[-1]
    name = re.sub(r'\.(?:prev|previous|pending|[0-9]+)$', '', name)
    return re.sub(r'\.(?:prev|previous|[0-9]+)\.log$', '.log', name)


def safe_entry(name):
    p = PurePosixPath(name.replace('\\', '/'))
    if p.is_absolute() or '..' in p.parts or ':' in name or '\x00' in name:
        raise ValueError('unsafe archive member path: ' + repr(name))


def iter_log_members(archive, *, max_member=MAX_MEMBER, max_total=MAX_TOTAL, on_member_error=None,
                     verify_directories=None):
    """(logical entry, original bytes, metadata), including verified cost volumes.

    No extraction and no arbitrary recursive ZIP parsing. Duplicate outer entries
    remain separate and carry their original occurrence ordinal.
    The default is fail-fast. An explicit error handler may retain a failure
    receipt and continue with other members; no failed payload is yielded.
    Handler mode also verifies directory entries, without yielding them as logs.
    A handler returning False rethrows the original failure. An explicit
    verify_directories=False keeps the ordinary file-only reader policy.
    """
    total = 0
    counts = collections.Counter()
    infos = archive.infolist()
    if len(infos) > MAX_MEMBERS:
        raise ValueError('member-count budget exceeded; coverage incomplete')
    if verify_directories is None:
        verify_directories = on_member_error is not None
    for info in infos:
        if info.is_dir() and not verify_directories:
            continue
        safe_entry(info.filename)
        if info.file_size > max_member or total + info.file_size > max_total:
            raise ValueError('read-byte budget exceeded at ' + info.filename + '; coverage incomplete')
        occurrence = counts[info.filename]
        counts[info.filename] += 1
        try:
            data = archive.read(info)  # ZipInfo preserves duplicate members; validates CRC.
        except ZIP_READ_ERRORS as ex:
            if on_member_error is None:
                raise
            total += info.file_size  # Failed reads still consume the declared byte budget.
            reason = str(ex)
            handled = on_member_error({'entry': info.filename, 'archiveEntryOccurrence': occurrence,
                'stage': 'outer-member-read', 'errorType': type(ex).__name__,
                'zipIntegrityFailure': isinstance(ex, ZIP_INTEGRITY_ERRORS),
                'reason': reason[:512], 'reasonTruncated': len(reason) > 512})
            if handled is False:
                raise
            continue
        total += len(data)
        if info.is_dir():
            continue
        nested = COST_ARCHIVE.fullmatch(info.filename) is not None
        yield info.filename, data, {'archiveEntryOccurrence': occurrence, 'containerOnly': nested}
        if not nested:
            continue
        budget_failed = False
        try:
            with zipfile.ZipFile(io.BytesIO(data)) as volume:
                expected = ['BEGIN.json', 'input-host-cost.log', 'END.json']
                if sorted(volume.namelist()) != sorted(expected):
                    raise ValueError('invalid or duplicate volume members: ' + info.filename)
                for member in volume.infolist():
                    if member.file_size > max_member or total + member.file_size > max_total:
                        budget_failed = True
                        raise ValueError('nested read-byte budget exceeded; coverage incomplete')
                    total += member.file_size
                begin_data, end_data = volume.read('BEGIN.json'), volume.read('END.json')
                if len(begin_data) > LINE_BYTES or len(end_data) > LINE_BYTES:
                    raise ValueError('oversize volume markers')
                begin, end = evidence_json_loads(begin_data), evidence_json_loads(end_data)
                if not isinstance(begin, dict) or not isinstance(end, dict):
                    raise ValueError('volume markers must be objects')
                payload = volume.read('input-host-cost.log')
                digest = hashlib.sha256(payload).hexdigest()
                volume_id = PurePosixPath(info.filename).name
                if (begin.get('schema') != 'ymcc-log-volume/1' or end.get('schema') != 'ymcc-log-volume/1'
                        or begin.get('record') != 'file_begin' or end.get('record') != 'file_end'
                        or begin.get('volumeId') != volume_id or end.get('volumeId') != volume_id
                        or begin.get('payloadEntry') != 'input-host-cost.log'
                        or any(marker.get('sha256') != digest or marker.get('sourceBytes') != len(payload)
                               for marker in (begin, end))
                        or end.get('completePayloadRetained') is not True):
                    raise ValueError('volume marker/size/SHA256 mismatch: ' + info.filename)
                yield info.filename + '!input-host-cost.log', payload, {
                    'archiveEntryOccurrence': occurrence, 'container': info.filename,
                    'volumeBegin': begin, 'volumeEnd': end, 'volumeIntegrity': 'verified',
                }
        except ZIP_READ_ERRORS + (ValueError, KeyError, TypeError) as ex:
            if on_member_error is None or budget_failed:
                raise
            reason = str(ex)
            handled = on_member_error({'entry': info.filename + '!input-host-cost.log',
                'archiveEntryOccurrence': occurrence, 'container': info.filename,
                'stage': 'nested-volume-validation', 'errorType': evidence_error_type(ex),
                'zipIntegrityFailure': isinstance(ex, ZIP_INTEGRITY_ERRORS),
                'reason': reason[:512], 'reasonTruncated': len(reason) > 512,**json_rejection_evidence(ex)})
            if handled is False:
                raise


def format_for(name):
    if name.startswith('steam/logs/'):
        return 'text-or-opaque'
    base = canonical_name(name)
    if base in JSONL or re.fullmatch(r'msi-controller-visibility-wake-\d+\.log', base):
        return 'json-lines'
    if base.endswith('.json'):
        return 'json-document'
    return 'text-or-opaque'


def byte_excerpt_index(raw, offset):
    """Bounded, exact ASCII views; no encoding, event or process inference.

    Inspect only the existing <=64KiB payload chunk. Keep first/last four
    candidate spans, with at most two non-overlapping spans per printable run.
    raw/rawBase64 remains the complete authoritative payload representation.
    """
    first, last = [], collections.deque(maxlen=BYTE_EXCERPTS // 2)
    runs = run_bytes = candidates = 0
    for match in ASCII_BYTE_RUN.finditer(raw):
        start, end = match.span()
        runs += 1
        run_bytes += end - start
        spans = [(start, min(start + BYTE_EXCERPT_BYTES, end))]
        if end - start > BYTE_EXCERPT_BYTES:
            spans.append((max(start + BYTE_EXCERPT_BYTES, end - BYTE_EXCERPT_BYTES), end))
        for begin, stop in spans:
            item = {'byteOffset': offset + begin, 'byteLength': stop - begin,
                    'ascii': raw[begin:stop].decode('ascii'),
                    'runByteOffset': offset + start, 'runByteLength': end - start}
            candidates += 1
            if len(first) < BYTE_EXCERPTS // 2:
                first.append(item)
            else:
                last.append(item)
    excerpts = first + list(last)
    return {'schemaVersion': 1, 'coordinates': 'uncompressed-member-bytes',
            'scope': 'sampled-ascii-byte-spans;not-lines-events-or-encoding-proof',
            'encodingInferred': False, 'nulBytes': raw.count(b'\x00'),
            'minRunBytes': 8, 'maxExcerpts': BYTE_EXCERPTS, 'maxExcerptBytes': BYTE_EXCERPT_BYTES,
            'asciiRuns': runs, 'asciiRunBytes': run_bytes, 'candidateExcerpts': candidates,
            'omittedCandidateExcerpts': candidates - len(excerpts),
            'unexcerptedAsciiRunBytes': run_bytes - sum(item['byteLength'] for item in excerpts),
            'payloadRetainedSeparately': True, 'excerpts': excerpts}


def payload_records(name, data, file_id, fmt):
    if fmt == 'json-document':
        try:
            if len(data) > 8 * 1024 * 1024:
                raise ValueError('document parse budget exceeded; raw bytes follow')
            yield {'record': 'json_document', 'fileId': file_id, 'value': evidence_json_loads(data)}
            return
        except (ValueError, UnicodeError) as ex:
            yield {'record': 'parse_notice', 'fileId': file_id, 'error': str(ex), 'rawBytesRetained': True,**json_rejection_evidence(ex)}
    stream = io.BytesIO(data)
    line, offset, continued = 1, 0, False
    while True:
        raw = stream.readline(LINE_BYTES)
        if not raw:
            break
        terminated = raw.endswith(b'\n')
        chunk = continued or (not terminated and stream.tell() < len(data))
        row = {'record': 'raw_line_chunk' if chunk else 'text_line', 'fileId': file_id,
               'line': line, 'byteOffset': offset, 'byteLength': len(raw), 'endsWithNewline': terminated}
        if fmt == 'text-or-opaque':
            row['lineBasis'] = 'LF-byte-delimited;not-an-event-count'
        try:
            text = raw.decode('utf-8')
            if '\x00' in text:
                raise UnicodeError('opaque/binary data')
            if fmt == 'json-lines' and not chunk and text.strip():
                try:
                    row['value'] = evidence_json_loads(text.removeprefix('\ufeff') if offset == 0 else text)
                    row['record'] = 'json_record'
                except ValueError as ex:
                    row.update(raw=text, parseError=str(ex), record='invalid_json_line')
                    row.update(json_rejection_evidence(ex))
            else:
                row['raw'] = text
        except UnicodeError:
            row['rawBase64'] = base64.b64encode(raw).decode('ascii')
            row['encoding'] = 'opaque-bytes'
        if 'rawBase64' in row or (fmt == 'text-or-opaque' and chunk):
            row['byteExcerptIndex'] = byte_excerpt_index(raw, offset)
        yield row
        offset += len(raw)
        continued = chunk and not terminated
        if terminated:
            line += 1
            continued = False


EXPORT_METADATA_ROLES = {
    'ai-log-index.jsonl': 'export-index-metadata',
    'AI-READING-GUIDE.json': 'export-reading-guide-metadata',
}


def metadata_payload_records(records, name, role):
    """Annotate reserved entry payloads; their contents remain untrusted evidence.

    Keep the old row kinds/value/raw bytes. A nested index fileId is not an ID
    issued by this reader, even when its text happens to match the wrapper.
    """
    for row in records:
        row.update(evidenceRole=role, runtimeEvent=False, sourceEntry=name,
                   fileIdNamespace='ai-reader-envelope')
        if role == 'export-index-metadata' and isinstance(row.get('value'), dict) and 'fileId' in row['value']:
            row['valueFileIdNamespace'] = 'export-index'
        yield row


def validated_ai_index(data, *, validation_info=None):
    """Return a complete typed index or reject it without a partial prefix.

    No manifest, duplicate index or late good frame supplies missing identity.
    Validation failures never certify the independently retained payloads.
    """
    frames = [evidence_json_loads(raw) for raw in data.splitlines()]
    if not all(isinstance(frame, dict) for frame in frames):
        raise ValueError('AI index frames must be objects')
    if (len(frames) < 2 or frames[0].get('record') != 'bundle_begin'
            or frames[-1].get('record') != 'bundle_end'
            or frames[-1].get('indexComplete') is not True
            or (len(frames) - 2) % 2):
        raise ValueError('incomplete AI index boundaries')
    boundaries = frames[0], frames[-1]
    schema_declared = any('schema' in frame for frame in boundaries)
    if schema_declared and any(frame.get('schema') != 'ymcc-ai-log-index/1' for frame in boundaries):
        raise ValueError('unknown or mismatched AI index schema')
    if (len(frames) - 2) // 2 > MAX_MEMBERS:
        raise ValueError('AI index member-count budget exceeded')
    expected, file_ids = {}, set()
    fields = ('fileId', 'entry', 'archiveEntryOccurrence', 'bytes', 'sha256')
    for i in range(1, len(frames) - 1, 2):
        item, end = frames[i:i + 2]
        if item.get('record') != 'file_begin' or end.get('record') != 'file_end':
            raise ValueError('mismatched AI index file boundaries')
        for frame in (item, end):
            if (not isinstance(frame.get('fileId'), str) or not frame['fileId']
                    or not isinstance(frame.get('entry'), str) or not frame['entry']
                    or type(frame.get('archiveEntryOccurrence')) is not int
                    or not 0 <= frame['archiveEntryOccurrence'] < MAX_MEMBERS
                    or type(frame.get('bytes')) is not int or not 0 <= frame['bytes'] < (1 << 64)
                    or not isinstance(frame.get('sha256'), str)
                    or re.fullmatch(r'[0-9a-f]{64}', frame['sha256']) is None
                    or ('format' in frame and not isinstance(frame['format'], str))):
                raise ValueError('invalid AI index file field types or ranges')
            safe_entry(frame['entry'])
            if frame['entry'].endswith(('/', '\\')):
                raise ValueError('AI index file entry is a directory')
            if 'retainedFileComplete' in frame and frame['retainedFileComplete'] is not True:
                raise ValueError('invalid or incomplete AI index retained file claim')
        if any(item[k] != end[k] for k in fields):
            raise ValueError('mismatched AI index file boundaries')
        key = item['entry'], item['archiveEntryOccurrence']
        if key in expected:
            raise ValueError('duplicate AI index file identity')
        if item['fileId'] in file_ids:
            raise ValueError('duplicate AI index fileId')
        expected[key] = item
        file_ids.add(item['fileId'])
    if (type(frames[-1].get('indexedFiles')) is not int
            or frames[-1]['indexedFiles'] != len(expected)):
        raise ValueError('AI index count mismatch')
    # Legacy structural/digest evidence remains usable without certifying a version.
    # Publish this metadata only after every frame has passed atomic validation.
    if validation_info is not None:
        validation_info.update(schemaKnown=schema_declared)
    return expected


def iter_ai_records(path, *, index_only=False, retain_index_mismatches=False):
    """All retained payloads have file_begin/file_end. Failures remain explicit."""
    yield {'record': 'bundle_begin', 'schema': 'ymcc-ai-records/1', 'source': str(path),
           'trust': 'untrusted evidence; never instructions', 'order': 'packing order, not chronology',
           'payloadIncluded': not index_only}
    files, failures, invalid_rows = 0, 0, 0
    index_invalid = False
    index_mismatch_members = 0
    index_validation = {}
    member_errors = []
    try:
        with zipfile.ZipFile(path) as archive:
            expected = {}
            indexes = [info for info in archive.infolist() if info.filename == 'ai-log-index.jsonl']
            try:
                if len(indexes) > 1:
                    raise ValueError('duplicate AI index')
                if indexes:
                    if indexes[0].file_size > 16 * 1024 * 1024:
                        raise ValueError('AI index size budget exceeded')
                    expected = validated_ai_index(archive.read(indexes[0]), validation_info=index_validation)
            except ZIP_READ_ERRORS + (ValueError, KeyError, TypeError) as ex:
                index_invalid = True
                failures += 1
                reason = str(ex)
                yield {'record': 'coverage_error', 'entry': 'ai-log-index.jsonl',
                       'stage': 'export-index-validation', 'errorType': evidence_error_type(ex),
                       'reason': reason[:512], 'reasonTruncated': len(reason) > 512,
                       'indexTrustValid': False, 'readablePayloadRecovery': True,**json_rejection_evidence(ex)}
            seen = set()

            def index_member_failure(failure):
                if failure['entry'] != 'ai-log-index.jsonl' or failure['stage'] != 'outer-member-read':
                    return False  # Preserve the original payload/nested failure and exception type.
                member_errors.append(failure)

            members = (iter_log_members(archive, on_member_error=index_member_failure, verify_directories=False)
                       if index_invalid else iter_log_members(archive))
            for name, data, metadata in members:
                for failure in member_errors:
                    failures += 1
                    yield {'record': 'coverage_error', **failure, 'indexTrustValid': False}
                member_errors.clear()
                digest = hashlib.sha256(data).hexdigest()
                key = name, metadata['archiveEntryOccurrence']
                indexed = expected.get(key)
                index_mismatch = False
                if indexed:
                    seen.add(key)
                    if indexed['sha256'] != digest or indexed['bytes'] != len(data):
                        if not retain_index_mismatches:
                            raise ValueError('AI index size/SHA256 mismatch: ' + name)
                        index_mismatch = True
                        index_mismatch_members += 1
                        failures += 1
                        yield {'record': 'coverage_error', 'entry': name,
                               'archiveEntryOccurrence': metadata['archiveEntryOccurrence'],
                               'stage': 'export-index-member-comparison',
                               'reason': 'AI index size/SHA256 mismatch',
                               'indexDeclaredBytes': indexed['bytes'], 'actualBytes': len(data),
                               'indexDeclaredSha256': indexed['sha256'], 'actualSha256': digest,
                               'indexTrustValid': False, 'readablePayloadRecovery': True}
                elif indexes and not index_invalid and 'container' not in metadata and name not in ('AI-READING-GUIDE.json', 'ai-log-index.jsonl'):
                    failures += 1
                    yield {'record': 'coverage_error', 'entry': name, 'reason': 'outer member absent from AI index'}
                file_id = 'f' + str(files)
                files += 1
                fmt = 'zip-volume' if metadata.get('containerOnly') else format_for(name)
                role = EXPORT_METADATA_ROLES.get(name) if 'container' not in metadata else None
                begin = {'record': 'file_begin', 'fileId': file_id, 'entry': name, 'format': fmt,
                       'bytes': len(data), 'sha256': digest, 'indexMatched': indexed is not None and not index_mismatch,
                       'indexDeclaredFormat': indexed.get('format') if indexed else None,
                       'formatDiffersFromIndex': bool(indexed and indexed.get('format') and indexed['format'] != fmt), **metadata}
                if role:
                    begin.update(evidenceRole=role, runtimeEvent=False, fileIdNamespace='ai-reader-envelope')
                if index_mismatch:
                    begin.update(indexTrustValid=False, indexVerification='retained-with-index-mismatch')
                if index_invalid:
                    begin.update(indexTrustValid=False, indexVerification='unavailable-invalid-index')
                if index_validation.get('schemaKnown') is False:
                    begin.update(indexSchemaKnown=False, indexSchema=None)
                yield begin
                counts = collections.Counter()
                if not index_only and not metadata.get('containerOnly'):
                    payload = payload_records(name, data, file_id, fmt)
                    if role:
                        payload = metadata_payload_records(payload, name, role)
                    for row in payload:
                        counts[row['record']] += 1
                        if index_mismatch:
                            row.update(indexTrustValid=False, indexVerification='retained-with-index-mismatch')
                        yield row
                invalid_rows += counts['invalid_json_line']
                end = {'record': 'file_end', 'fileId': file_id, 'entry': name, 'sha256': digest,
                       'bytes': len(data), 'endsWithNewline': not data or data.endswith(b'\n'),
                       'records': dict(counts), 'retainedFileComplete': True, 'processSessionComplete': None,
                       'payloadIncluded': not index_only and not metadata.get('containerOnly'),
                       'payloadDelegatedToNested': metadata.get('containerOnly', False)}
                if role:
                    end.update(evidenceRole=role, runtimeEvent=False, fileIdNamespace='ai-reader-envelope')
                if index_mismatch:
                    end.update(indexTrustValid=False, indexMatched=False, indexVerification='retained-with-index-mismatch')
                if index_invalid:
                    end.update(indexTrustValid=False, indexVerification='unavailable-invalid-index')
                if index_validation.get('schemaKnown') is False:
                    end.update(indexSchemaKnown=False, indexSchema=None)
                yield end
            for failure in member_errors:
                failures += 1
                yield {'record': 'coverage_error', **failure, 'indexTrustValid': False}
            member_errors.clear()
            for name, occurrence in expected.keys() - seen:
                failures += 1
                yield {'record': 'coverage_error', 'entry': name, 'archiveEntryOccurrence': occurrence,
                       'reason': 'indexed member missing from archive'}
    except ZIP_READ_ERRORS + (ValueError, KeyError, TypeError) as ex:
        for failure in member_errors:
            failures += 1
            yield {'record': 'coverage_error', **failure, 'indexTrustValid': False}
        failures += 1
        yield {'record': 'coverage_error', 'errorType': evidence_error_type(ex), 'reason': str(ex),**json_rejection_evidence(ex)}
    footer = {'record': 'bundle_end', 'files': files, 'coverageComplete': failures == 0,
           'coverageErrors': failures, 'invalidJsonRowsRetained': invalid_rows, 'processSessionComplete': None,
           'sourceValidityCertified': False, 'payloadIncluded': not index_only,
           'coverageScope': 'retained-archive-members-only;not-live-source-completeness'}
    if index_mismatch_members:
        footer.update(indexTrustValid=False, readablePayloadRecovery=True,
                      indexVerification='payload-mismatch', indexMismatchMembers=index_mismatch_members)
    if index_invalid:
        footer.update(indexTrustValid=False, readablePayloadRecovery=True)
    if index_validation.get('schemaKnown') is False:
        footer.update(indexSchemaKnown=False, indexSchema=None)
    yield footer


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--index-only', action='store_true')
    parser.add_argument('--retain-index-mismatches', action='store_true',
                        help='retain readable members despite index size/SHA mismatch;coverage stays incomplete')
    args = parser.parse_args()
    # Refuse accidental overwrite of source evidence, even when the path is aliased.
    if args.output.resolve() == args.archive.resolve():
        parser.error('output must differ from original archive')
    if args.output.exists() and args.archive.exists() and args.output.samefile(args.archive):
        parser.error('output aliases original archive')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('w', encoding='utf-8', newline='\n') as output:
        for record in iter_ai_records(args.archive, index_only=args.index_only, retain_index_mismatches=args.retain_index_mismatches):
            output.write(json.dumps(record, ensure_ascii=True, allow_nan=False, separators=(',', ':')) + '\n')
    print(json.dumps(record, allow_nan=False))
    return 0 if record.get('coverageComplete') else 3


if __name__ == '__main__':
    raise SystemExit(main())
