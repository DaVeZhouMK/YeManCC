from pathlib import Path
import copy,json,sys,tempfile,unittest,zipfile
sys.path.insert(0,str(Path(__file__).resolve().parent));import log_lane_audit as a
class ExecGuardTests(unittest.TestCase):
 def row(self,guard='missing'):
  t={'stage':'exec-drop','epochMatch':True,'loopAllowed':True,'childOwned':False}
  if guard!='missing':t['execGuard']=guard
  return {'where':'gamepad-admission','trace':t}
 def guard(self,values=(False,None,None,None)):
  return dict(schema=1,scope=a.EXEC_GUARD_SCOPE,**dict(zip(a.EXEC_GUARD_FIELDS,values)))
 def test_legacy_is_not_reconstructed(self):
  r=a.frontend_exec_guard_record(self.row());self.assertEqual(r['state'],'legacy-missing');self.assertIsNone(r['guard']);self.assertIsNone(r['firstFailure'])
 def test_four_first_failures_and_child_true(self):
  for values,reason in zip(a.EXEC_GUARD_PATTERNS,a.EXEC_GUARD_FAILURES):
   r=a.frontend_exec_guard_record(self.row(self.guard(values)));self.assertEqual(r['state'],'reported-first-failure');self.assertEqual(r['firstFailure'],reason);self.assertFalse(r['actionOrDeliveryProven'])
 def test_unsupported_schema(self):
  for v in (None,True,2,'1',1.0):
   g=self.guard();g['schema']=v;self.assertEqual(a.frontend_exec_guard_record(self.row(g))['state'],'unsupported-schema')
 def test_wrong_scope(self):
  g=self.guard();g['scope']='post-decision';self.assertEqual(a.frontend_exec_guard_record(self.row(g))['state'],'unsupported-scope')
 def test_missing_fields_never_mean_skipped(self):
  for k in a.EXEC_GUARD_FIELDS:
   g=self.guard();g.pop(k);r=a.frontend_exec_guard_record(self.row(g));self.assertEqual(r['state'],'invalid-fields');self.assertIsNone(r['guard'])
 def test_wrong_scalar_types(self):
  for v in (0,1,'false',[],{}):
   g=self.guard();g['overlaySessionMatch']=v;self.assertEqual(a.frontend_exec_guard_record(self.row(g))['state'],'invalid-fields')
 def test_impossible_short_circuit_combinations(self):
  for v in ((False,True,None,None),(True,None,False,None),(True,True,True,False),(None,None,None,None),(True,True,False,True)):
   self.assertEqual(a.frontend_exec_guard_record(self.row(self.guard(v)))['state'],'invalid-fields')
 def test_null_and_invalid_shape(self):
  self.assertEqual(a.frontend_exec_guard_record(self.row(None))['state'],'reported-null');self.assertEqual(a.frontend_exec_guard_record(self.row([]))['state'],'invalid-shape')
 def test_unrelated_rows_and_nonobject_trace(self):
  self.assertIsNone(a.frontend_exec_guard_record({'where':'other','trace':{'stage':'exec-drop'}}));self.assertIsNone(a.frontend_exec_guard_record({'where':'gamepad-admission','trace':True}));self.assertIsNone(a.frontend_exec_guard_record({'where':'gamepad-admission','trace':{'stage':'exec-run'}}))
 def test_bounded_witnesses(self):
  r=a.frontend_exec_guard_evidence([(i,self.row(self.guard())) for i in range(100)]);self.assertEqual(r['rows'],100);self.assertEqual(len(r['witnesses']),16);self.assertEqual(r['omittedWitnesses'],84);self.assertTrue(r['missingIsUnknown'])
 def test_no_join_or_privilege_from_extra_fields(self):
  g=self.guard();g['nativeAccepted']=True;g['session']='private';r=a.frontend_exec_guard_record(self.row(g));self.assertFalse(r['nativeAdmissionProven']);self.assertNotIn('session',r['guard'])
 def test_actual_archive_rotated_lane_wiring(self):
  source=(Path(__file__).resolve().parents[1]/'native/main.cpp').read_text('utf-8-sig')
  with tempfile.TemporaryDirectory(prefix='ymcc-exec-guard-') as d:
   p=Path(d)/'x.zip'
   with zipfile.ZipFile(p,'w') as z:z.writestr('frontend-trace.log.1',json.dumps(self.row(self.guard()))+'\n')
   r=a.audit_zip(p,a.source_policy(source))
  file=next(x for x in r['files'] if x['file']=='frontend-trace.log.1');self.assertEqual(file['executionGuardEvidence']['counts'],{'reported-first-failure':1})
if __name__=='__main__':unittest.main()
