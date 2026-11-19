"""Contract tests for offline USBIP setup snapshot resolution."""
import base64
import hashlib
import io
import json
import unittest
import zipfile
import zlib
from unittest.mock import patch

from usbip_setup_log_reader import (CARRIERS, INDEX, MAX_RELEASE_DETAIL, RELEASE_ENCODING,
                                    read_members, release_details, resolve_references, scope_coverage)

OLD, ACTIVE = CARRIERS


def line(detail, *, phase='registered-driver', clipped=False, available=None, **overrides):
    row = {'t': 70, 'kind': 'usbip-setup-evidence', 'phase': phase,
           'scopeId': '0123456789abcdef0123456789abcdef', 'scopeHostPid': 4,
           'hostInstanceId': 'host-a', 'runId': 'run-b', 'epoch': 1, 'command': 'PREPARE_TARGET',
           'persona': 'steamdeck', 'data': {'detail': detail, 'textTruncated': clipped, 'available': available}}
    row.update(overrides)
    return json.dumps(row, separators=(',', ':')).encode() + b'\n'


def begin(i=0, **kwargs):
    return line(f'snapshot=begin index={i} status=observed version=99.0', **kwargs)


def end(i=0, **kwargs):
    return line(f'snapshot=end index={i} sameAsBegin=1', **kwargs)


def lookup(members):
    refs = list(resolve_references(members))
    if len(refs) != 1:
        raise AssertionError(refs)
    return refs[0]


def scope_end(attempted, written, *, dropped=0, truncated=0, errors=0, **kwargs):
    return line(f'attemptedBeforeEnd={attempted} writtenBeforeEnd={written} progress=0 stages=0 '
                f'dropped={dropped} textTruncated={truncated} logErrorsBeforeEnd={errors} '
                'elapsedMs=3.0;observations-not-availability-or-loaded-memory-image-proof',
                phase='scope-end', **kwargs)


class ScopeCoverageTests(unittest.TestCase):
    def test_writer_losses_do_not_turn_count_agreement_into_a_retention_gap(self):
        rotation = line('begin', phase='scope-begin', t=90)
        current = begin(clipped=True) + scope_end(3, 2, dropped=4, truncated=1, errors=1, t=60)
        view = scope_coverage({ACTIVE: current, OLD: rotation})['commands']['PREPARE_TARGET']
        self.assertEqual(view['boundaries'], {'paired': 1})
        self.assertEqual(view['retainedCount'], {'same-count': 1})
        self.assertEqual(view['endCounterPositive'], {'dropped': 1, 'textTruncated': 1, 'logErrorsBeforeEnd': 1})
        self.assertEqual(view['firstWitness'], {})

    def test_missing_retained_rows_and_wrong_scope_do_not_backfill(self):
        current = begin() + scope_end(3, 3) + scope_end(1, 1, hostInstanceId='other') + begin(scopeHostPid=True)
        result = scope_coverage({ACTIVE: current})
        self.assertEqual(result['parsedSetupRows'], 4)
        self.assertEqual(result['unboundOrInvalidPhaseRows'], 1)
        view = result['commands']['PREPARE_TARGET']
        self.assertEqual(view['boundaries'], {'end-only': 2})
        self.assertEqual(view['retainedCount'], {'fewer-retained-rows': 2})
        self.assertEqual(view['endCounterPositive'], {})
        self.assertEqual(view['firstWitness']['end-only:fewer-retained-rows'], {
            'end': {'entry': ACTIVE, 'line': 2, 't': 70}, 'retainedBeforeEnd': 1, 'reportedWrittenBeforeEnd': 3})

    def test_unknown_end_duplicate_and_reversed_boundaries(self):
        start = line('begin', phase='scope-begin')
        for ending in (scope_end(2, 1), scope_end(1, 1, clipped=True), scope_end(129, 129),
                       scope_end(1, 1, available=False), line('bad', phase='scope-end')):
            with self.subTest(ending=ending):
                view = scope_coverage({ACTIVE: start + ending})['commands']['PREPARE_TARGET']
                self.assertEqual(view['retainedCount'], {'end-counters-unknown': 1})
                self.assertIsNone(view['firstWitness']['paired:end-counters-unknown']['reportedWrittenBeforeEnd'])
        ending = scope_end(1, 1)
        for body, boundary in ((start + ending + ending, 'duplicate-boundary'), (ending + start, 'boundary-out-of-order'),
                               (begin() + start + ending, 'boundary-out-of-order')):
            with self.subTest(boundary=boundary):
                view = scope_coverage({ACTIVE: body})['commands']['PREPARE_TARGET']
                self.assertEqual(view['boundaries'], {boundary: 1})
                self.assertEqual(view['retainedCount'], {'not-comparable': 1})

    def test_extra_rows_and_absent_end_stay_distinct(self):
        open_prepare = line('begin', phase='scope-begin') + scope_end(1, 1)[:-1]
        release = {'command': 'RELEASE_LOCAL'}
        extra_release = line('begin', phase='scope-begin', **release) + begin(**release) + scope_end(1, 1, **release)
        commands = scope_coverage({OLD: open_prepare, ACTIVE: extra_release})['commands']
        self.assertEqual(commands['PREPARE_TARGET']['retainedCount'], {'no-end-retained': 1})
        self.assertEqual(commands['RELEASE_LOCAL']['retainedCount'], {'more-retained-rows': 1})

    def test_scope_budget_withholds_partial_totals(self):
        with patch('usbip_setup_log_reader.MAX_COVERAGE_SCOPES', 1):
            view = scope_coverage({ACTIVE: begin() + begin(scopeHostPid=5)})
        self.assertEqual(view, {'schemaVersion': 1, 'state': 'scope-limit-exceeded', 'maxScopes': 1})


class SnapshotReaderTests(unittest.TestCase):
    def test_rotated_begin_preserves_own_end_time(self):
        # Clock reversal does not change append order. Slot 1 is not selected.
        ref = lookup({ACTIVE: end(t=60), OLD: begin(1) + begin(t=90)})
        self.assertEqual(ref['status'], 'RESOLVED')
        self.assertEqual(ref['begin'], {'entry': OLD, 'line': 2, 't': 90})
        self.assertEqual(ref['end'], {'entry': ACTIVE, 'line': 1, 't': 60})
        self.assertEqual(ref['reconstructedDetail'], 'snapshot=end index=0 status=observed version=99.0')

    def test_legacy_reference_and_enum(self):
        for phase, detail in [('disk-image', 'status=observed bytes=3'),
                              ('service-enum', 'status=observed registeredCount=0 limit=8 omitted=0')]:
            with self.subTest(phase=phase):
                data = line('snapshot=begin ' + detail, phase=phase) + line('snapshot=end sameAsBegin=1', phase=phase)
                self.assertEqual(lookup({ACTIVE: data})['reconstructedDetail'], 'snapshot=end ' + detail)

    def test_all_scope_fields_and_slot_must_match(self):
        for prefix in [begin(1), begin(scopeId='fedcba9876543210fedcba9876543210'), begin(scopeHostPid=5),
                       begin(epoch=2), begin(hostInstanceId='other'), begin(runId='other'), begin(command='RELEASE_LOCAL'),
                       begin(persona='elite'), begin(phase='registered-binding')]:
            with self.subTest(prefix=prefix):
                ref = lookup({ACTIVE: prefix + end()})
                self.assertEqual(ref['status'], 'UNKNOWN')
                self.assertEqual(ref['reason'], 'begin-not-retained')

    def test_prior_and_unique_begin_and_end(self):
        self.assertEqual(lookup({ACTIVE: end() + begin()})['reason'], 'begin-not-prior')
        self.assertEqual(lookup({OLD: begin(), ACTIVE: begin() + end()})['reason'], 'ambiguous-snapshot-rows')
        full_end = line('snapshot=end index=0 status=observed version=99.0')
        self.assertEqual(lookup({ACTIVE: begin() + end() + full_end})['reason'], 'ambiguous-snapshot-rows')

    def test_clipped_error_and_invalid_boolean_do_not_resolve(self):
        for prefix in [begin(clipped=True), begin(available=False), line('snapshot=begin index=0 status=missing')]:
            self.assertEqual(lookup({ACTIVE: prefix + end()})['reason'], 'begin-not-complete-success')
        for invalid_end in [end(clipped=True), end(scopeHostPid=True), end(available=False)]:
            self.assertEqual(lookup({ACTIVE: begin() + invalid_end})['reason'], 'invalid-or-truncated-reference')
        self.assertEqual(list(resolve_references({ACTIVE: begin() + end()[:-1]})), [])

    def test_release_detail_recovers_complete_bytes_and_preserves_legacy_status(self):
        def release(detail, **kwargs):
            return line(detail, phase='release-begin', command='RELEASE_LOCAL', **kwargs)
        def encoded(raw):
            return RELEASE_ENCODING + base64.b64encode(raw).decode('ascii')
        complete = ('设备;instance=HID\\VID_28DE&PID_1205;' * 50)
        valid = encoded(zlib.compress(complete.encode('utf-8')))
        rows = [release(valid), release('pnp-not-observed'), release('pnp-hid-ids=partial', clipped=True)]
        results = list(release_details({OLD: rows[0], ACTIVE: b''.join(rows[1:])}))
        self.assertEqual([r['status'] for r in results], ['decoded', 'plain', 'truncated'])
        self.assertEqual([r['detail'] for r in results], [complete, 'pnp-not-observed', 'pnp-hid-ids=partial'])
        for bad in (RELEASE_ENCODING + ':', encoded(zlib.compress(b'x' * (MAX_RELEASE_DETAIL + 1))),
                    encoded(zlib.compress(b'abc')[:-1]), encoded(zlib.compress(b'abc') + b'junk'),
                    encoded(zlib.compress(b'\xff'))):
            with self.subTest(bad=bad):
                result, = release_details({ACTIVE: release(bad)})
                self.assertEqual((result['status'], result['detail']), ('invalid', None))
        for body in (release(valid, clipped=True), release(valid, available=False), release(valid, scopeHostPid=True)):
            result, = release_details({ACTIVE: body})
            self.assertEqual((result['status'], result['detail']), ('invalid', None))
        self.assertEqual(list(release_details({ACTIVE: line(valid, phase='release-begin')})), [])

    def test_index_uses_zero_occurrence_and_rejects_mismatch(self):
        body = begin() + end()
        fields = dict(fileId='f0', entry=ACTIVE, archiveEntryOccurrence=0, bytes=len(body),
                      sha256=hashlib.sha256(body).hexdigest())
        frames = [{'record': 'bundle_begin', 'schema': 'ymcc-ai-log-index/1'},
                  dict(fields, record='file_begin'), dict(fields, record='file_end'),
                  {'record': 'bundle_end', 'schema': 'ymcc-ai-log-index/1', 'indexComplete': True, 'indexedFiles': 1}]
        for actual in (body, body + b'\n'):
            buf = io.BytesIO()
            with zipfile.ZipFile(buf, 'w') as z:
                z.writestr(ACTIVE, actual)
                z.writestr(INDEX, '\n'.join(json.dumps(frame) for frame in frames))
            with zipfile.ZipFile(buf) as z:
                if actual == body:
                    self.assertEqual(read_members(z), {ACTIVE: body})
                else:
                    with self.assertRaisesRegex(ValueError, 'size/SHA'):
                        read_members(z)


if __name__ == '__main__':
    unittest.main()
