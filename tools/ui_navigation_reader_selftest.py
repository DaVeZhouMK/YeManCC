import json
from pathlib import Path
import tempfile
import unittest
import zipfile

from log_lane_audit import UiActionLinkEvidence, audit_zip


class UiNavigationReaderTests(unittest.TestCase):
    birth = '134394455061639721'

    def row(self, stage, sequence=1, action='page-next', **detail):
        return dict(event='frontend-trace', where='gamepad-admission', writerEnvelopeSchema=1, writerPid=314,
                    writerProcessCreation100ns=self.birth, trace=dict(stage=stage, action=action,
                    uiActionLink=dict(schema=1, session='314:121', sequence=sequence), **detail))

    def audit(self, front, *, anchor=True):
        collector = UiActionLinkEvidence()
        collector.observe('frontend-trace.log.1', enumerate(front, 1), {'archiveEntryOccurrence': 0})
        if anchor:
            native = dict(self.row('nav-start')['trace'], event='gamepad-ui-input-emitted', target='yemancc',
                          pid=314, writerProcessCreation100ns=self.birth)
            collector.observe('native-detail.log', [(1, native)], {'archiveEntryOccurrence': 0})
        return collector.evidence()

    def test_exact_begin_and_resolved_failure(self):
        result = self.audit([self.row('nav-start'), self.row('nav-rejected', routerFailure=16)])
        self.assertEqual(result['keysByState'], {'matched-reported-stages': 1})
        match = result['examples'][0]
        self.assertEqual(match['frontendStages'], {'nav-start': 1, 'nav-rejected': 1})
        self.assertEqual(match['firstWitnessByStage']['nav-rejected']['routerFailure'], 16)
        self.assertNotIn('nav-done', match['frontendStages'])

    def test_gate_and_exception_keep_separate_action_keys(self):
        result = self.audit([self.row('nav-not-started', gate='after-queue', reason='page-lock'),
                             self.row('nav-fail', sequence=2, failureAt='after-push')])
        self.assertEqual(result['keysByState'], {'matched-reported-stages': 1, 'frontend-only': 1})
        gate, error = result['examples']
        self.assertEqual(gate['firstWitnessByStage']['nav-not-started']['reason'], 'page-lock')
        self.assertEqual(error['firstWitnessByStage']['nav-fail']['failureAt'], 'after-push')

    def test_writer_mismatch_and_legacy_are_not_joined(self):
        other = dict(self.row('nav-done'), writerProcessCreation100ns='12')
        legacy = self.row('nav-start'); del legacy['trace']['uiActionLink']
        bad = self.row('nav-done', action='back')
        result = self.audit([other, legacy, bad])
        self.assertEqual(result['keysByState'], {'frontend-only': 1, 'native-only': 1})
        self.assertEqual(result['counts']['legacyNavigationRowsWithoutLink'], 1)
        self.assertEqual(result['counts']['invalidLinkOrWriterRows'], 1)

    def test_invalid_optional_fields_do_not_become_witnesses(self):
        result = self.audit([self.row('nav-rejected', routerFailure=True),
                             self.row('nav-not-started', gate='after-queue', reason='page-command-handled')])
        seen = result['examples'][0]['firstWitnessByStage']
        self.assertNotIn('routerFailure', seen['nav-rejected'])
        self.assertNotIn('reason', seen['nav-not-started'])

    def test_archive_rotation_retains_failed_stage(self):
        with tempfile.TemporaryDirectory() as directory:
            file = Path(directory) / 'navigation.zip'
            with zipfile.ZipFile(file, 'w') as archive:
                archive.writestr('frontend-trace.log.1', json.dumps(self.row('nav-fail', failureAt='router-push'))+'\n')
            result = audit_zip(file, {'baseEvents': []})['commonEvidence']['uiActionLinkEvidence']
        self.assertEqual(result['examples'][0]['firstWitnessByStage']['nav-fail']['file'], 'frontend-trace.log.1')
        self.assertTrue(result['missingStageIsUnknown'])
        self.assertFalse(result['postAcceptedOrNavigationProven'])


if __name__ == '__main__':
    unittest.main()
