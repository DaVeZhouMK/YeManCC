import hashlib
import json
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from zipfile import ZipFile

from sleep_kernel_history_reader import MEMBER, NS, PROVIDER, read_archive


def xml(event_id, record, fields=()):
    payload = ''.join(f'<Data Name="{name}">{value}</Data>' for name, value in fields)
    return (f'<Event xmlns="{NS["e"]}"><System><Provider Name="{PROVIDER}"/>'
            f'<EventID>{event_id}</EventID><EventRecordID>{record}</EventRecordID>'
            '<TimeCreated SystemTime="2026-11-19T02:07:24.5820001Z"/>'
            '<Channel>System</Channel></System>'
            f'<EventData>{payload}</EventData></Event>')


class KernelHistoryReaderTests(unittest.TestCase):
    def run_zip(self, rows=None, capture_status='events-collected', exhausted=True):
        payload = {} if rows is None else {'windowsKernelPowerHistory': {
            'schemaVersion': 1, 'provider': PROVIDER, 'channel': 'System', 'lookbackMs': 10800000,
            'queryExhausted': exhausted, 'status': capture_status, 'events': [{'xml': text} for text in rows],
        }}
        with TemporaryDirectory() as directory:
            path = Path(directory) / 'capture.zip'
            with ZipFile(path, 'x') as archive:
                archive.writestr(MEMBER, json.dumps(payload))
            result = read_archive(path)
        self.assertFalse(result['automaticRepairAuthorized'])
        self.assertFalse(result['requestAttributionProven'])
        self.assertFalse(result['completeWindowsCoverageProven'])
        return result

    def test_confirmed_entry_and_explicit_no_entry_do_not_depend_on_drips(self):
        # Sleep interval from the retained Win5 R21 field projection; XML envelope is synthetic.
        entered = [('SleepEntered', 'true'), ('SleepDurationInUs', '670890398'),
                   ('IsCsSessionInProgressOnExit', 'false'), ('ScenarioInstanceIdV2', '9007199254740993')]
        no_entry = [('SleepEntered', 'false'), ('SleepDurationInUs', '0'), ('IsCsSessionInProgressOnExit', '0')]
        rows = [xml(507, 102, entered), xml(507, 101, entered + [('SleepHwDripsResidencyInUs', '0')]),
                xml(507, 100, no_entry)]
        result = self.run_zip(rows)
        self.assertEqual(result['windowsExitStages'], {'entered': 2, 'not-entered': 1})
        first = result['events'][0]
        self.assertEqual(first['recordId'], '102')
        self.assertEqual(first['timeUtc'], '2026-11-19T02:07:24.5820001Z')
        self.assertEqual(first['xml'], rows[0])
        self.assertEqual(first['xmlSha256'], hashlib.sha256(rows[0].encode()).hexdigest())
        self.assertEqual(first['scenarioInstanceIdV2'], '9007199254740993')

    def test_missing_or_conflicting_stage_fields_are_indeterminate(self):
        closed = [('IsCsSessionInProgressOnExit', 'false')]
        rows = [xml(507, 99, closed + [('SleepDurationInUs', '0')]),
                xml(507, 98, closed + [('SleepDurationInUs', '0'), ('SleepEntered', 'true')]),
                xml(507, 97, closed + [('SleepDurationInUs', '0'), ('SleepEntered', 'false'), ('SleepEntered', 'true')]),
                xml(507, 96, [('IsCsSessionInProgressOnExit', '1'), ('SleepDurationInUs', '0'), ('SleepEntered', 'false')])]
        result = self.run_zip(rows)
        self.assertEqual(result['windowsExitStages'], {'unknown': 4})
        self.assertEqual(result['events'][2]['duplicateFields'], ['SleepEntered'])

    def test_missing_truncated_or_discarded_history_does_not_assign_an_outcome(self):
        self.assertEqual(self.run_zip()['status'], 'raw-history-absent')
        self.assertEqual(self.run_zip([], 'no-matching-records')['windowsExitStages'], {})
        rows = [xml(506, 91), xml(187, 90)]
        result = self.run_zip(rows, 'checkpoint-budget-reached', False)
        self.assertEqual(result['captureStatus'], 'checkpoint-budget-reached')
        self.assertFalse(result['queryExhausted'])
        self.assertEqual(result['windowsExitStages'], {})
        self.assertEqual(self.run_zip(rows, 'export-power-observation-changed')['events'], [])

    def test_malformed_or_wrong_provider_cannot_supply_a_stage(self):
        rows = [xml(507, 88).replace(PROVIDER, 'OtherProvider'), '<not-xml',
                '<!DOCTYPE x [<!ENTITY entity "text">]>' + xml(507, 87)]
        result = self.run_zip(rows)
        self.assertEqual(result['status'], 'event-decode-errors')
        self.assertEqual(result['invalidEvents'], 3)
        self.assertEqual(result['windowsExitStages'], {})


if __name__ == '__main__':
    unittest.main()
