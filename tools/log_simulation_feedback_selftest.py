"""Synthetic ordinary ZIP coverage: no hardware, settings, product or commands."""
from pathlib import Path
import hashlib,json,tempfile,unittest,zipfile
from log_simulation_feedback import audit_archive,MAX_LINE
class FeedbackTests(unittest.TestCase):
 def setUp(self):self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
 def tearDown(self):self.temp.cleanup()
 def pack(self,members):
  p=self.root/'sample.zip'
  with zipfile.ZipFile(p,'w',compression=zipfile.ZIP_STORED) as z:
   for k,v in members.items():z.writestr(k,v)
  return p
 def manifest(self,backend=True):return 'timeZoneBiasMinutes=-480\nappVersion=0.0.35\nnativeSha256='+('a'*64)+'\nhostDllSha256='+('b'*64)+'\nruntimeManifestSha256='+('d'*64)+'\n'+('fanBackendDllSha256='+('c'*64)+'\n' if backend else '')
 def lines(self,*rows):return ''.join(json.dumps(r)+'\n' for r in rows)
 def test_all_five_nodes_remain_evidence_not_acceptance(self):
  p=self.pack({'YeManCC-export-manifest.txt':self.manifest(),'native-lifecycle.log':self.lines({'event':'machine-identity','manufacturer':'GPD','product':'G1618-05','deviceClass':'HandheldCompanion.Devices.GPDWin5','time':'2026-10-19T11:00:00'},{'event':'wake-input-chain-first-frame','powerGeneration':2,'time':'2026-10-19T11:01:00'},{'event':'gamepad-setLed','physicalEffectVerified':False,'time':'2026-10-19T11:01:01'})})
  r=audit_archive(p);self.assertEqual(len(r['nodes']),5);self.assertFalse(r['physicalAcceptance']);self.assertEqual(r['commandsExecuted'],0);self.assertEqual(r['timeZoneOffset'],'+08:00');self.assertTrue(r['archiveStable']);self.assertNotIn('CONFIRMED_PRODUCT_DEFECT',[n['classification'] for n in r['nodes']])
 def test_absent_backend_is_field_gap_never_filled_from_host(self):
  p=self.pack({'YeManCC-export-manifest.txt':self.manifest(False),'native-lifecycle.log':self.lines({'event':'machine-identity','manufacturer':'GPD'})});r=audit_archive(p);self.assertEqual(r['versionTuple']['backendSha256OrUnknown'],'unknown');self.assertIn('backendSha256OrUnknown',r['nodes'][0]['difference']['missingOrUnproven']);self.assertEqual(r['nodes'][0]['classification'],'LOG_FIELD_GAP')
 def test_empty_archive_does_not_pass_any_unobserved_domain(self):
  r=audit_archive(self.pack({'YeManCC-export-manifest.txt':self.manifest()}));self.assertTrue(all(n['classification']=='NOT_OBSERVED' for n in r['nodes']))
 def test_200_supported_false_literal_and_envelope_remain_separate(self):
  p=self.pack({'YeManCC-export-manifest.txt':self.manifest(),'fan-api.log':self.lines({'event':'api.response','details':{'path':'/api/handshake','status':200,'receiptValid':True,'receipt':{'ok':True,'supported':False},'response':{'supported':False,'hardwareCapable':False}}})});r=audit_archive(p);e=r['nodes'][0]['observedSequence'][0];self.assertFalse(e['fields']['details.receipt']['supported']);self.assertFalse(e['fields']['details.response']['hardwareCapable'])
 def test_free_text_keys_and_payload_commands_are_omitted(self):
  secret='SECRET_NEVER_COPY';p=self.pack({'YeManCC-export-manifest.txt':self.manifest(),'settings.json':secret,'steam/logs/controller.txt':secret,'input-capture.jsonl':secret,'fan-api.log':self.lines({'event':'api.response','details':{'path':'/api/handshake','status':200,'request':{'leaseId':secret},'sessionToken':secret,'error':secret,'command':secret,'response':{'supported':False}}})});r=audit_archive(p);self.assertNotIn(secret,json.dumps(r));self.assertEqual(r['sourceBytesRead'],len(self.manifest().encode())+len(self.lines({'event':'api.response','details':{'path':'/api/handshake','status':200,'request':{'leaseId':secret},'sessionToken':secret,'error':secret,'command':secret,'response':{'supported':False}}}).encode()))
 def test_false_string_does_not_become_boolean(self):
  p=self.pack({'YeManCC-export-manifest.txt':self.manifest(),'fan-api.log':self.lines({'event':'api.response','details':{'path':'/api/handshake','response':{'supported':'false'}}})});r=audit_archive(p);self.assertFalse(any('supported' in v for n in r['nodes'] for e in n['observedSequence'] for v in e['fields'].values()))
 def test_owner_is_hashed_and_never_transferred_to_another_row(self):
  owner='private-owner-key';p=self.pack({'YeManCC-export-manifest.txt':self.manifest(),'yeman-fan-host-runtime.log':self.lines({'eventName':'hc.fan-dispatch.begin','hostInstanceId':owner},{'eventName':'hc.fan-dispatch.callback-returned'})});r=audit_archive(p);seq=r['nodes'][1]['observedSequence'];self.assertIn('ownerCorrelationSha256',seq[0]);self.assertNotIn('ownerCorrelationSha256',seq[1]);self.assertNotIn(owner,json.dumps(r))
 def test_backend_version_mismatch_is_not_product_defect(self):
  p=self.pack({'YeManCC-export-manifest.txt':self.manifest()});r=audit_archive(p,{'backendSha256OrUnknown':'e'*64});self.assertTrue(all(n['classification']=='VERSION_MISMATCH' for n in r['nodes']))
 def test_unsafe_zip_path_rejected_before_read(self):
  p=self.pack({'../fan-api.log':'{}'})
  with self.assertRaisesRegex(ValueError,'UNSAFE_MEMBER'):audit_archive(p)
 def test_oversize_row_is_reported_not_silently_complete(self):
  p=self.pack({'fan-api.log':'x'*(MAX_LINE+1)});r=audit_archive(p);self.assertIn('LINE_SIZE_LIMIT',[d['code'] for d in r['diagnostics']]);self.assertEqual(r['status'],'INCOMPLETE_EVIDENCE_COVERAGE')
 def test_first_frame_after_first32_rows_does_not_become_missing(self):
  rows=[{'event':'fan-host-resume-observe','generation':2} for _ in range(40)]+[{'event':'wake-input-chain-first-frame','powerGeneration':2}];p=self.pack({'YeManCC-export-manifest.txt':self.manifest(),'native-lifecycle.log':self.lines(*rows)});r=audit_archive(p);n=r['nodes'][2];self.assertEqual(n['recordCount'],41);self.assertEqual(len(n['observedSequence']),32);self.assertNotIn('retained-first-effective-input-after-wake',n['difference']['missingOrUnproven'])
 def test_host_payload_is_not_runtime_manifest(self):
  manifest=self.manifest().replace('runtimeManifestSha256=','hostPayloadManifestSha256=');r=audit_archive(self.pack({'YeManCC-export-manifest.txt':manifest}));self.assertEqual(r['versionTuple']['runtimeManifestSha256OrUnknown'],'unknown');self.assertEqual(r['exportHostPayloadManifestSha256'],'d'*64)
 def test_machine_identities_from_different_writers_are_not_merged(self):
  p=self.pack({'YeManCC-export-manifest.txt':self.manifest()+'processId=7\n','native-lifecycle.log':self.lines({'event':'machine-identity','pid':6,'manufacturer':'GPD','product':'old'},{'event':'machine-identity','pid':7,'manufacturer':'ASUS','product':'new'})});r=audit_archive(p);self.assertEqual(len(r['modelEvidence']),2);self.assertFalse(r['modelEvidence'][0]['matchesExportNativePid']);self.assertTrue(r['modelEvidence'][1]['matchesExportNativePid']);self.assertEqual(r['modelEvidence'][0]['values']['product'],'old');self.assertFalse(r['modelEvidence'][1]['processBirthBindingProven'])
 def test_action_logs_are_prioritized_before_dense_samples(self):
  p=self.pack({'YeManCC-export-manifest.txt':self.manifest(),'gyro-motion.log':'{}\n'*50001,'yeman-fan-host-runtime.log':self.lines({'eventName':'hc.fan-dispatch.begin','processId':77,'hostInstanceId':'opaque-owner','details':{'provider':'HC'}})});r=audit_archive(p);n=r['nodes'][1];self.assertEqual(n['recordCount'],1);self.assertEqual(n['observedSequence'][0]['writerProcessId'],77);self.assertEqual(n['observedSequence'][0]['fields']['details']['provider'],'HC');self.assertIn('TOTAL_ROW_LIMIT',[d['code'] for d in r['diagnostics']])
 def test_host_cpu_row_keeps_single_process_scope_and_unknown_writer(self):
  p=self.pack({'YeManCC-export-manifest.txt':self.manifest(),'input-host-cost.log':self.lines({'t':1792116398464,'windowMs':1000,'procCpuMsPerSec':125,'usbipTransport':None})});r=audit_archive(p);e=r['nodes'][4]['observedSequence'][0];self.assertEqual(e['fields']['record']['procCpuMsPerSec'],125);self.assertEqual(e['cpuScope'],'one-InputHost-process-only;not-logger-or-whole-product');self.assertNotIn('writerProcessId',e);self.assertIn('schema-derived',e['eventProvenance']);self.assertEqual(r['nodes'][4]['classification'],'LOG_FIELD_GAP')
 def test_cost_counter_null_does_not_become_zero(self):
  p=self.pack({'YeManCC-export-manifest.txt':self.manifest(),'input-host-cost.log':self.lines({'t':1792116398464,'windowMs':1000,'procCpuMsPerSec':None,'writerPid':77})});e=audit_archive(p)['nodes'][4]['observedSequence'][0];self.assertFalse(e['processCpuRateKnown']);self.assertNotIn('procCpuMsPerSec',e['fields']['record']);self.assertEqual(e['writerProcessId'],77)
 def test_placeholder_owner_does_not_create_proven_correlation(self):
  p=self.pack({'YeManCC-export-manifest.txt':self.manifest(),'yeman-fan-host-runtime.log':self.lines({'eventName':'hc.fan-dispatch.begin','hostInstanceId':'not-observed','details':{'ScopeId':'unknown'}})});n=audit_archive(p)['nodes'][1];self.assertNotIn('ownerCorrelationSha256',n['observedSequence'][0]);self.assertIn('control-owner-correlation',n['difference']['missingOrUnproven'])
 def test_handshake_and_polling_are_not_control_lifecycle(self):
  p=self.pack({'YeManCC-export-manifest.txt':self.manifest(),'fan-api.log':self.lines({'event':'api.response','details':{'path':'/api/handshake','response':{'supported':False}}},{'event':'api.response','details':{'path':'/api/state','response':{'state':'Ready'}}})});r=audit_archive(p);self.assertEqual(r['nodes'][0]['recordCount'],1);self.assertEqual(r['nodes'][1]['classification'],'NOT_OBSERVED')
 def test_existing_bytes_and_unqualified_time_preserved(self):
  p=self.pack({'YeManCC-export-manifest.txt':self.manifest(),'native-lifecycle.log':self.lines({'event':'gamepad-controller-wake','time':'2026-10-19T11:02:03.004'})});before=p.read_bytes();r=audit_archive(p);self.assertEqual(p.read_bytes(),before);self.assertEqual(r['logArchiveSha256'],hashlib.sha256(before).hexdigest());self.assertEqual(r['nodes'][4]['timeRangeWithTimezone']['perMember']['native-lifecycle.log']['first'],'2026-10-19T11:02:03.004')
if __name__=='__main__':unittest.main()
