"""Synthetic read-only ZIP audit regression. No product execution or real devices."""
import json
import hashlib
import io
import tempfile
import unittest
import zipfile
from pathlib import Path
from ai_log_reader import iter_ai_records, iter_log_members
from log_lane_audit import audit_zip, source_policy, pid, process_identity, stamp, repeated_guard_successes, late_hide_episodes, repeated_frame_observations, canonical_log_name, native_process_cost_summary


class AuditTests(unittest.TestCase):
    def test_speedhack_is_external_timestamped_text_including_rotations(self):
        with tempfile.TemporaryDirectory() as root:
            archive = Path(root) / 'text.zip'
            with zipfile.ZipFile(archive, 'w') as z:
                for name in ('speedhack.log', 'speedhack.log.1', 'speedhack.previous.log'):
                    z.writestr(name, '2026-10-24 01:48:14.207 QUEUE op=1\nRAW diagnostic text\n')
                z.writestr('native-detail.log', '{invalid genuine JSON row}\n')
            report = audit_zip(archive, {'baseEvents': [], 'baseEventCount': 0})
            self.assertEqual(len(report['coverageWarnings']), 1)
            self.assertEqual(report['coverageWarnings'][0]['file'], 'native-detail.log')
            self.assertEqual([r['format'] for r in report['files'] if r['file'].startswith('speedhack')], ['plain-text'] * 3)

    def test_source_comments_are_not_events(self):
        p = source_policy('const char* kBaseEvents[] = {"real", // "fake"\n /* "also-fake" */ "url//literal"};')
        self.assertEqual(p['baseEvents'], ['real', 'url//literal'])

    def test_writer_identity_does_not_use_business_pid(self):
        self.assertEqual(pid({'writerPid':7,'pid':12345}), '7')
        self.assertEqual(pid({'pid':7}), '7')
        self.assertEqual(pid({}), 'unknown')

    def test_observer_and_host_process_identity(self):
        row = {'event':'directinput-device','observerPid':13748,'pid':6402,'hidPid':6402}
        self.assertEqual(process_identity(row), ('13748','observerPid'))
        row.pop('observerPid')
        self.assertEqual(pid(row), 'unknown')
        self.assertEqual(pid({'pid':6402}, 'msi-controller-visibility.log.prev'), 'unknown')
        self.assertEqual(pid({'usbipTransport':{'hostPid':12208},'t':1791916347737}), '12208')
        self.assertEqual(pid({'writerPid':False,'observerPid':-1,'event':'wgi-device','pid':6402}), 'unknown')
        self.assertEqual(pid({'parentPid':9868}), 'unknown')

    def test_sdk_scope_pid_is_schema_bound_and_writer_has_precedence(self):
        row = {'kind':'xbox-sdk-evidence', 'scopeHostPid':4992, 'hostInstanceId':'host-a', 'runId':'run-a'}
        for name in ('input-host-xbox-sdk.log', 'input-host-xbox-sdk.log.1', 'input-host-xbox-sdk.log.prev'):
            self.assertEqual(process_identity(row, name), ('4992', 'scopeHostPid'))
            self.assertEqual(process_identity({**row, 'writerPid':77}, name), ('77','writerPid'))
        for name in ('other.log', 'input-host-cost.log', 'native-lifecycle.log', 'msi-controller-visibility.log'):
            self.assertEqual(pid(row, name), 'unknown')
        self.assertEqual(pid({**row, 'kind':'other'}, 'input-host-xbox-sdk.log'), 'unknown')
        for invalid in (True, False, 0, -1, None, 'not-a-pid'):
            self.assertEqual(pid({**row,'scopeHostPid':invalid}, 'input-host-xbox-sdk.log'), 'unknown')

    def test_cost_writer_identity_survives_null_transport(self):
        row = {'writerPid':11272, 'hostInstanceId':'host-a', 'runId':'run-a', 'usbipTransport':None}
        self.assertEqual(process_identity(row,'input-host-cost.log'),('11272','writerPid'))
        self.assertEqual(pid({k:v for k,v in row.items() if k!='writerPid'},'input-host-cost.log'),'unknown')
        self.assertEqual(pid({**row,'usbipTransport':{'hostPid':27}},'input-host-cost.log'),'11272')

    def test_native_pid_is_writer_and_host_pid_is_subject(self):
        row = {'pid':10024, 'hostPid':11740, 'event':'input-host-switch-audit'}
        for name in ('native-lifecycle.log', 'native-detail.log.1', 'native-lifecycle.log.prev'):
            self.assertEqual(process_identity(row, name), ('10024', 'pid'))
        self.assertEqual(pid({**row, 'writerPid':5}, 'native-lifecycle.log'), '5')
        self.assertEqual(pid({'hostPid':11740}, 'native-lifecycle.log'), 'unknown')
        self.assertEqual(pid({'hostPid':11740,'pid':6402}, 'msi-controller-visibility.log'), 'unknown')

    def test_frame_periodic_observation_is_not_a_transition(self):
        source = dict(snapshotKnown=True, inputSource='xinput', powerGeneration=1,
                      topologyGeneration=7, primarySlot=0, admittedMask=1, live=True, readContextStable=True)
        row = dict(event='input-host-frame-evidence', pid=7, powerGeneration=1, formatVersion=1,
                   source=source, hostSubmission='frame-accepted', semanticNeutral=False, frameProof={})
        rows = [(1,row), (2,{**row, 'observedTick':1000}),
                (3,{**row, 'source':{**source,'topologyGeneration':8}}),
                (4,{**row,'pid':8}), (5,{**row,'formatVersion':2,'snapshotReason':'periodic'}),
                (6,{**row,'formatVersion':2,'snapshotReason':'state-change'}),
                (7,{**row,'source':{}}),
                (8,{'event':'boot-single-instance-acquired','pid':7}), (9,row)]
        self.assertEqual([i for i,_ in repeated_frame_observations(rows)], [2,5])

    def test_clock_domains_are_not_guessed(self):
        self.assertEqual(stamp({'utcEpochMs':1791916346323}), '2026-10-13T18:32:26.323+00:00')
        self.assertEqual(stamp({'t':1791916347737}, 'input-host-cost.log.1'), '2026-10-13T18:32:27.737+00:00')
        self.assertIsNone(stamp({'t':1791916347737}, 'input-capture.jsonl'))
        self.assertIsNone(stamp({'t':95984}, 'unknown.log'))
        self.assertIsNone(stamp({'utcEpochMs':True}))
        self.assertIsNone(stamp({'utcEpochMs':10**100}))

    def test_periodic_guard_success_only(self):
        def row(state, **detail):
            return {'event':'oem-vendor-stack','state':state,'pid':7,'powerGeneration':1,**detail}
        rows=[row('wake-guard-started'),row('wake-guard-pass',ok=True,requestedEpoch=1,durationMs=3),
              row('wake-guard-pass',ok=True,requestedEpoch=2,durationMs=4),
              row('wake-guard-pass',ok=False),row('wake-guard-pass',ok=False),
              row('wake-guard-pass',ok=True),row('wake-guard-pass',ok=True),
              row('wake-guard-pass',ok='unknown'),row('wake-guard-pass',ok=True),
              row('wake-guard-started'),row('wake-guard-pass',ok=True),
              row('wake-guard-pass',ok=True,powerGeneration=2),row('applied'),row('applied')]
        self.assertEqual([line for line,_ in repeated_guard_successes(list(enumerate(rows,1)))],[3,7])

    def test_transaction_identity_and_real_stage_receipts(self):
        events=['late-hide-enqueue','late-hide-dequeue','late-hide-exec-decision','late-hide-complete']
        rows=[]
        for run in ['host-A','host-B']:
            for token in [1,2]:
                for event in events:
                    rows.append({'event':event,'pid':7,'powerGeneration':2,'runId':run,'hostGen':1,
                                 'recordGen':2,'sourceGen':3,'reason':'source-return','queueToken':token,
                                 'decision':'refused-window','outcome':'skipped',
                                 'time':'2026-10-14T02:38:19.00'+str(token)})
        rows.append({'event':'late-hide-complete','pid':7,'queueToken':1})
        result=late_hide_episodes(list(enumerate(rows,1)),'7','native-lifecycle.log')
        self.assertEqual(len(result['episodes']),2)
        self.assertEqual(result['rowsWithIncompleteIdentity'],[17])
        for episode in result['episodes']:
            self.assertEqual(episode['uniquePositiveQueueTokens'],2)
            self.assertEqual(episode['tokensWithExactlyOneEachStage'],2)
            self.assertEqual(episode['tokensWithRepeatedDequeueDecisionOrComplete'],[])
            self.assertEqual(episode['completionSpanMs'],1)
            self.assertEqual(episode['decisions'],{'refused-window':2})
            self.assertEqual(episode['completionOutcomes'],{'skipped':2})
        rows.insert(4, dict(rows[3]))
        result=late_hide_episodes(list(enumerate(rows,1)),'7','native-lifecycle.log')
        first=next(x for x in result['episodes'] if x['identity']['runId']=='host-A')
        self.assertEqual(first['tokensWithExactlyOneEachStage'],1)
        self.assertEqual(first['tokensWithRepeatedDequeueDecisionOrComplete'],[1])

    def test_observer_file_and_guard_rule_integrate(self):
        with tempfile.TemporaryDirectory(prefix='ymcc-log-roles-') as folder:
            path=Path(folder)/'fixture.zip'
            guard=[{'event':'oem-vendor-stack','state':'wake-guard-pass','ok':True,'pid':7,
                    'powerGeneration':1,'requestedEpoch':i,'durationMs':i} for i in range(10)]
            observer=[{'event':'directinput-device','observerPid':13748,'pid':6402,'utcEpochMs':1791916346323},
                      {'event':'wgi-device','pid':6402,'utcEpochMs':1791916346324}]
            with zipfile.ZipFile(path,'w') as z:
                z.writestr('YeManCC-export-manifest.txt','processId=7')
                z.writestr('native-lifecycle.log','\n'.join(map(json.dumps,guard))+'\n')
                z.writestr('msi-controller-visibility.log.prev','\n'.join(map(json.dumps,observer))+'\n')
                z.writestr('log-export-availability.json',json.dumps({'rendererDiagnosticDeliveryFailures':4}))
            report=audit_zip(path, {'baseEvents':[]})
            self.assertEqual(len(report['violations']),1)
            self.assertEqual(report['violations'][0]['counts']['all'],9)
            f=next(x for x in report['files'] if x['file'].startswith('msi-controller'))
            self.assertEqual(f['lane'],'observer-detail')
            self.assertEqual(f['counts']['byPid'],{'13748':1,'unknown':1})
            self.assertTrue(f['firstTime'].endswith('+00:00'))
            self.assertEqual(report['availabilityReceipt']['rendererDiagnosticDeliveryFailures'],4)

    def test_known_host_text_and_both_rotation_styles(self):
        with tempfile.TemporaryDirectory(prefix='ymcc-log-formats-') as folder:
            path=Path(folder)/'fixture.zip'
            with zipfile.ZipFile(path,'w') as z:
                z.writestr('YeManCC-export-manifest.txt','processId=7')
                z.writestr('yeman-fan-host20261015.log','2026-10-15 17:00:00 [INF] Device firmware observed\n')
                z.writestr('yeman-fan-host-runtime.12.log',json.dumps({'eventName':'host.start','processId':8})+'\n')
                z.writestr('msi-controller-visibility-wake-2.log.12',json.dumps({'event':'observer.start','observerPid':9})+'\n')
                z.writestr('unknown.log','bad row\n')
            report=audit_zip(path,{'baseEvents':[]})
            files={f['file']:f for f in report['files']}
            text=files['yeman-fan-host20261015.log']
            self.assertEqual((text['format'],text['lane'],text['lineCount']),('plain-text','hc-text',1))
            host=files['yeman-fan-host-runtime.12.log']
            self.assertEqual((host['rotation'],host['lane'],host['counts']['byPid']),(True,'fan-host-mixed',{'8':1}))
            self.assertEqual(files['msi-controller-visibility-wake-2.log.12']['counts']['byPid'],{'9':1})
            self.assertEqual([x['file'] for x in report['coverageWarnings']],['unknown.log'])
        self.assertEqual(canonical_log_name('yeman-fan-host-runtime.previous.log'),'yeman-fan-host-runtime.log')
        self.assertEqual(canonical_log_name('native-detail.log.12'),'native-detail.log')
        self.assertEqual(canonical_log_name('native-detail.log.abc'),'native-detail.log.abc')

    def test_native_cpu_counter_provenance_and_weighted_windows(self):
        def row(begin,span,user,creation=100,**more):
            return {'event':'input-idle-window','writerPid':7,'nativeProcessCost':{
                'pid':7,'processCreation100ns':creation,'cpuRateAvailable':True,
                'intervalBeginTick':begin,'sampleTick':begin+span,'intervalMs':span,
                'userDelta100ns':user,'kernelDelta100ns':0,'cpuCounterSource':'GetProcessTimes',
                'cpuCounterUnit':'100ns',**more}}
        rows=[row(0,1000,1000000),row(1000,2000,4000000),row(0,1000,3000000,creation=200),
              row(3000,1000,1000000,cpuCounterSource='wrong'),
              row(4000,1000,-1),row(5000,1000,1000000,pid=8),
              {'event':'input-idle-window','writerPid':7}]
        report=native_process_cost_summary(list(enumerate(rows,1)),'7','virtual-gamepad.log')
        first,second=report['processInstances']
        self.assertEqual(first['rows'],2)
        self.assertAlmostEqual(first['weightedCpuMsPerSec'],500/3)
        self.assertAlmostEqual(first['oneLogicalCoreEquivalent'],1/6)
        self.assertEqual(first['medianCpuMsPerSec'],150)
        self.assertEqual(first['p95CpuMsPerSec'],200)
        self.assertEqual(second['weightedCpuMsPerSec'],300)
        self.assertEqual(sum(report['rejectedOrMissingRows'].values()),4)
        overlap=native_process_cost_summary([(1,rows[0]),(2,rows[0])],'7','virtual-gamepad.log')['processInstances'][0]
        self.assertTrue(overlap['overlappingIntervals'])
        self.assertIsNone(overlap['weightedCpuMsPerSec'])
        self.assertIsNone(overlap['oneLogicalCoreEquivalent'])

    def test_archive_evidence(self):
        with tempfile.TemporaryDirectory(prefix='ymcc-log-audit-') as folder:
            path = Path(folder) / 'fixture.zip'
            def row(event, pid=7, **detail):
                return {'event': event, 'pid': pid, 'powerGeneration': 1, **detail}
            rows = [row('boot-single-instance-acquired', time='2026-10-13T00:00:00'),
                    row('rog.hid-discovery-summary', matches=0),
                    row('rog.hid-discovery-summary', matches=0),
                    row('rog.hid-discovery-summary', pid=8, matches=0),
                    row('rog.hid-discovery-summary', pid=8, matches=0),
                    row('msi-rear-source-observation', snapshotReason='context-change'),
                    row('msi-rear-source-observation', snapshotReason='raw-input-change'),
                    row('gamepad-fallback-tick', connected=True),
                    row('input-host-command-result', command='SUBMIT_FRAME', ok=False),
                    row('input-host-command-result', command='SUBMIT_FRAME', ok=True)]
            with zipfile.ZipFile(path, 'w') as z:
                z.writestr('YeManCC-export-manifest.txt', 'processId=7\r\nnativeSha256=EXE-HASH\r\n')
                z.writestr('native-lifecycle.log', '\n'.join(json.dumps(x) for x in rows)+'\n')
                z.writestr('native-lifecycle.log.prev', json.dumps(row('system-xinput-heartbeat', pid=8))+'\n')
                z.writestr('input-capture.jsonl', json.dumps({'kind':'sample','event':{'counter':1}})+'\nbad-middle\n'+json.dumps({'kind':'sample'})+'\n{"unfinished":')
                z.writestr('app.log', '[INFO] rog.hid-discovery.matches=0\n[INFO] rog.hid-discovery.matches=0\n')
                z.writestr('fan-api.log', json.dumps({'event':'lifecycle.heartbeat-success'})+'\n')
                z.writestr('steam/logs/controller.txt', 'external ungoverned log')
            report = audit_zip(path, {'baseEvents': [], 'baseEventCount': 0})
            self.assertIsNone(report['crcFirstBadEntry'])
            self.assertEqual(report['manifest']['processId'], '7')
            native = next(f for f in report['files'] if f['file']=='native-lifecycle.log')
            self.assertEqual(native['counts']['currentPidMatch'], 8)
            repeated = next(x for x in report['violations'] if x['kind']=='unchanged-discovery-snapshot-in-native-low')
            self.assertEqual(repeated['counts']['all'], 2)
            self.assertEqual(repeated['counts']['currentPidMatch'], 1)
            self.assertEqual(repeated['counts']['otherPid'], 1)
            self.assertEqual(len(report['violations']), 5)
            self.assertEqual(report['eventVolumeCandidates'], [])
            issues = report['coverageWarnings'][0]['invalidRows']
            self.assertEqual(issues, [{'line':2,'kind':'invalid-json-row'},{'line':4,'kind':'partial-or-invalid-tail'}])
            capture = next(f for f in report['files'] if f['file']=='input-capture.jsonl')
            self.assertEqual(capture['events'][0]['event'], 'sample')
            self.assertEqual(capture['counts']['pidUnknown'], 2)
            text = next(x for x in report['violations'] if x['file']=='app.log')
            self.assertIsNone(text['currentPidMatch'])
            self.assertEqual(report['excludedExternalFiles'], ['steam/logs/controller.txt'])




class AiReaderTests(unittest.TestCase):
    def volume(self, payload=b'{"writerPid":12,"t":1,"seq":1}\n', *, bad_digest=False):
        digest = hashlib.sha256(payload).hexdigest()
        name = 'input-host-cost.log.archive.0639283912345678901.' + digest[:24] + '.zip'
        common = dict(schema='ymcc-log-volume/1', volumeId=name, sourceBytes=len(payload), sha256=digest)
        begin = dict(common, record='file_begin', payloadEntry='input-host-cost.log')
        end = dict(common, record='file_end', completePayloadRetained=True)
        if bad_digest:
            end['sha256'] = '0' * 64
        out = io.BytesIO()
        with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as archive:
            archive.writestr('BEGIN.json', json.dumps(begin))
            archive.writestr('input-host-cost.log', payload)
            archive.writestr('END.json', json.dumps(end))
        return name, out.getvalue()

    def read(self, entries, *, index_only=False):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / 'fixture.zip'
            with zipfile.ZipFile(path, 'w') as archive:
                for name, data in entries:
                    archive.writestr(name, data)
            return list(iter_ai_records(path, index_only=index_only))

    def test_real_nested_format_is_verified_and_audited_as_host_detail(self):
        name, data = self.volume()
        records = self.read([(name, data)])
        self.assertTrue(records[-1]['coverageComplete'])
        self.assertEqual([row['value']['seq'] for row in records if row['record'] == 'json_record'], [1])
        self.assertEqual(sum(row['record'] == 'file_begin' for row in records), 2)
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / 'fixture.zip'
            with zipfile.ZipFile(path, 'w') as archive:
                archive.writestr(name, data)
            result = audit_zip(path, {'baseEvents': [], 'baseEventCount': 0})
            self.assertEqual(len(result['files']), 1)
            self.assertEqual(result['files'][0]['lane'], 'host-detail')
            self.assertEqual(result['files'][0]['volumeIntegrity'], 'verified')

    def test_bad_nested_digest_is_an_explicit_coverage_failure(self):
        name, data = self.volume(bad_digest=True)
        records = self.read([(name, data)])
        self.assertFalse(records[-1]['coverageComplete'])
        self.assertTrue(any(row['record'] == 'coverage_error' for row in records))
        self.assertFalse(any(row['record'] == 'json_record' for row in records))

    def test_text_pretty_json_and_invalid_json_are_not_conflated(self):
        records = self.read([('steam/logs/controller.txt', 'normal text\n'),
                             ('state.json', '{\n "pid":999\n}\n'),
                             ('native-lifecycle.log', '{invalid\n{"writerPid":7,"pid":999}\n')])
        self.assertTrue(records[-1]['coverageComplete'])
        self.assertEqual(records[-1]['invalidJsonRowsRetained'], 1)
        self.assertEqual(sum(row['record'] == 'json_document' for row in records), 1)
        value = next(row['value'] for row in records if row['record'] == 'json_record')
        self.assertEqual(value, {'writerPid':7,'pid':999})
        self.assertFalse(any('writerPid' in row for row in records))  # No guessed normalization.

    def test_opaque_and_oversize_lines_retain_exact_bytes(self):
        data = b'x' * (65536 * 2 + 10) + b'\n\xff\xfe\nunterminated'
        records = self.read([('steam/logs/opaque.log', data)])
        import base64
        rows = [row for row in records if 'raw' in row or 'rawBase64' in row]
        actual = b''.join(base64.b64decode(row['rawBase64']) if 'rawBase64' in row else row['raw'].encode('utf-8') for row in rows)
        self.assertEqual(actual, data)
        self.assertTrue(records[-1]['coverageComplete'])
        self.assertFalse([row for row in records if row['record'] == 'file_end'][0]['endsWithNewline'])

    def test_index_only_is_explicit_about_omitted_payload(self):
        records = self.read([('app.log', 'text\n')], index_only=True)
        self.assertTrue(records[-1]['coverageComplete'])
        self.assertFalse(records[-1]['payloadIncluded'])
        self.assertFalse(any(row['record'] in ('text_line','json_record') for row in records))

    def test_traversal_is_not_extracted_or_claimed_complete(self):
        records = self.read([('../unsafe.log', 'text')])
        self.assertFalse(records[-1]['coverageComplete'])

    def test_nested_limits_fail_explicitly(self):
        name, data = self.volume()
        buffer = io.BytesIO()
        with zipfile.ZipFile(buffer, 'w') as archive:
            archive.writestr(name, data)
        buffer.seek(0)
        with zipfile.ZipFile(buffer) as archive:
            with self.assertRaises(ValueError):
                list(iter_log_members(archive, max_member=1))

    def test_index_missing_end_is_not_certified(self):
        records = self.read([('ai-log-index.jsonl', '{"record":"bundle_begin"}\n')])
        self.assertFalse(records[-1]['coverageComplete'])

    def test_index_digest_and_missing_member_are_both_checked(self):
        payload = b'text\n'
        begin = dict(record='file_begin',fileId='f0',entry='app.log',archiveEntryOccurrence=0,
                     bytes=len(payload),sha256=hashlib.sha256(payload).hexdigest())
        end = dict(begin, record='file_end')
        frames = [dict(record='bundle_begin'), begin, end, dict(record='bundle_end',indexComplete=True,indexedFiles=1)]
        index = '\n'.join(json.dumps(row) for row in frames)
        self.assertTrue(self.read([('app.log',payload),('ai-log-index.jsonl',index)])[-1]['coverageComplete'])
        self.assertFalse(self.read([('app.log',b'bad'),('ai-log-index.jsonl',index)])[-1]['coverageComplete'])
        self.assertFalse(self.read([('ai-log-index.jsonl',index)])[-1]['coverageComplete'])


if __name__ == '__main__':
    unittest.main()
