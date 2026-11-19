from pathlib import Path
import copy,json,sys,tempfile,unittest,zipfile
sys.path.insert(0,str(Path(__file__).resolve().parent))
import log_lane_audit as a
class OverlayEntryTests(unittest.TestCase):
 def row(self):
  counts={k:0 for k in a.OVERLAY_ENTRY_COUNTERS}
  return {'rendererDiagnosticDeliveryEvidence':{'overlaySessionEntry':dict(schema=1,status='projected',known=True,scope='renderer-reported-session-entry-counts-only',counterLimit=(1<<53)-1,counts=counts,atLimit={k:False for k in counts},counterFieldsKnown=4)}}
 def value(self,row):return row['rendererDiagnosticDeliveryEvidence']['overlaySessionEntry']
 def test_missing_unknown(self):
  r=a.renderer_overlay_session_entry_evidence({});self.assertEqual(r['state'],'not-observed');self.assertTrue(all(v is None for v in r['counts'].values()))
 def test_measured_zero_not_action(self):
  r=a.renderer_overlay_session_entry_evidence(self.row());self.assertEqual(r['state'],'projected');self.assertEqual(r['counts']['activePass'],0);self.assertFalse(r['zeroIsNoInputProof']);self.assertFalse(r['actionOrDeliveryProven'])
 def test_partial(self):
  row=self.row();v=self.value(row);v.update(status='partial-counters',known=False,counterFieldsKnown=3);v['counts']['activePass']=None;v['atLimit']['activePass']=None;r=a.renderer_overlay_session_entry_evidence(row);self.assertEqual(r['state'],'partial-counters');self.assertIsNone(r['counts']['activePass'])
 def test_bad_types(self):
  for bad in (True,-1,1.0,'1',1<<53,[],{}):
   row=self.row();self.value(row)['counts']['activePass']=bad;r=a.renderer_overlay_session_entry_evidence(row);self.assertEqual(r['state'],'invalid-fields');self.assertIsNone(r['counts']['activePass'])
 def test_saturation(self):
  row=self.row();v=self.value(row);v['counts']['activePass']=(1<<53)-1;v['atLimit']['activePass']=True;r=a.renderer_overlay_session_entry_evidence(row);self.assertTrue(r['atLimit']['activePass']);self.assertFalse(r['nativeAdmissionProven'])
 def test_schema_scope_and_status(self):
  for field,bad,state in [('schema',True,'unsupported-schema'),('scope','actions','unsupported-scope'),('status','success','unsupported-status')]:
   row=self.row();self.value(row)[field]=bad;self.assertEqual(a.renderer_overlay_session_entry_evidence(row)['state'],state)
 def test_invalid_native_known_metadata(self):
  for field,bad in [('known',False),('counterFieldsKnown',3),('counterLimit',True)]:
   row=self.row();self.value(row)[field]=bad;self.assertEqual(a.renderer_overlay_session_entry_evidence(row)['state'],'invalid-fields')
 def test_null_and_native_rejection(self):
  row=self.row();row['rendererDiagnosticDeliveryEvidence']['overlaySessionEntry']=None;self.assertEqual(a.renderer_overlay_session_entry_evidence(row)['state'],'reported-null')
  row=self.row();self.value(row)['status']='unsupported-schema';self.assertEqual(a.renderer_overlay_session_entry_evidence(row)['state'],'unsupported-schema')
 def test_archive_integration(self):
  source=(Path(__file__).resolve().parents[1]/'native/main.cpp').read_text('utf-8-sig')
  with tempfile.TemporaryDirectory(prefix='ymcc-overlay-reader-') as d:
   p=Path(d)/'evidence.zip'
   with zipfile.ZipFile(p,'w') as z:z.writestr('log-export-availability.json',json.dumps(self.row()))
   r=a.audit_zip(p,a.source_policy(source))
  self.assertEqual(r['commonEvidence']['rendererOverlaySessionEntry']['state'],'projected')
if __name__=='__main__':unittest.main()
