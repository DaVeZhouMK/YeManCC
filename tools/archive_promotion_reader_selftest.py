from pathlib import Path
import copy,sys,unittest
sys.path.insert(0,str(Path(__file__).resolve().parent));import log_lane_audit as a
class ArchivePromotionTests(unittest.TestCase):
 def attempt(self,observed=False,accepted=None,error=None,entered=False,returned=None,readable=None):
  return dict(promotionEvidence=dict(schema=1,scope='original-MoveFileExW-and-post-promotion-validation;not-current-path-or-unique-zip-proof',replacementRequested=False,currentPathAbsenceProven=False,uniqueFinalZipProven=False,promotion=dict(observed=observed,accepted=accepted,win32Error=error),postPromotionValidation=dict(entered=entered,returned=returned,readableAndNonempty=readable)))
 def test_missing_null_and_invalid(self):
  for value,state in [({},'missing'),(None,'missing'),({'promotionEvidence':None},'null'),({'promotionEvidence':True},'invalid-type')]:self.assertEqual(a.archive_promotion_record(value)['state'],state)
 def test_unobserved_is_not_success(self):
  r=a.archive_promotion_record(self.attempt());self.assertEqual(r['promotion'],{'state':'not-observed'});self.assertEqual(r['postPromotionValidation'],{'state':'not-entered'})
 def test_normal_two_successful_returns(self):
  r=a.archive_promotion_record(self.attempt(True,True,0,True,True,True));self.assertEqual(r['state'],'reported-original-promotion-observations');self.assertTrue(r['postPromotionValidation']['readableAndNonempty'])
 def test_refused_promotion_does_not_become_validation_false(self):
  r=a.archive_promotion_record(self.attempt(True,False,32));self.assertFalse(r['promotion']['accepted']);self.assertEqual(r['postPromotionValidation']['state'],'not-entered')
 def test_accepted_move_then_false_read_is_distinct(self):
  r=a.archive_promotion_record(self.attempt(True,True,0,True,True,False));self.assertTrue(r['promotion']['accepted']);self.assertFalse(r['postPromotionValidation']['readableAndNonempty'])
 def test_entered_without_return_stays_unknown(self):
  r=a.archive_promotion_record(self.attempt(True,True,0,True,False));self.assertEqual(r['postPromotionValidation']['state'],'entered-return-not-observed')
 def test_validation_cannot_follow_refused_or_unobserved_move(self):
  for v in [self.attempt(True,False,32,True,True,False),self.attempt(False,None,None,True,True,True)]:self.assertEqual(a.archive_promotion_record(v)['state'],'invalid-fields')
 def test_bool_schema_and_unknown_scope_rejected(self):
  for value in [True,1.0,'1',2]:
   v=self.attempt();v['promotionEvidence']['schema']=value;self.assertEqual(a.archive_promotion_record(v)['state'],'unsupported-schema')
  v=self.attempt();v['promotionEvidence']['scope']='future';self.assertEqual(a.archive_promotion_record(v)['state'],'unsupported-scope')
 def test_error_is_exact_unsigned_dword(self):
  r=a.archive_promotion_record(self.attempt(True,False,(1<<32)-1));self.assertEqual(r['promotion']['win32Error'],(1<<32)-1)
  for value in [True,-1,0.0,'32',1<<32]:self.assertEqual(a.archive_promotion_record(self.attempt(True,False,value))['state'],'invalid-fields')
 def test_accepted_move_does_not_accept_error(self):self.assertEqual(a.archive_promotion_record(self.attempt(True,True,32))['state'],'invalid-fields')
 def test_unobserved_return_payload_conflicts(self):
  for v in [self.attempt(False,False,0),self.attempt(False,None,32)]:self.assertEqual(a.archive_promotion_record(v)['state'],'invalid-fields')
 def test_unentered_and_unreturned_payload_conflicts(self):
  for v in [self.attempt(True,True,0,False,False,None),self.attempt(True,True,0,True,False,False)]:self.assertEqual(a.archive_promotion_record(v)['state'],'invalid-fields')
 def test_all_observation_booleans_require_bool(self):
  for slot,field in [('promotion','observed'),('promotion','accepted'),('postPromotionValidation','entered'),('postPromotionValidation','returned'),('postPromotionValidation','readableAndNonempty')]:
   v=self.attempt(True,True,0,True,True,True);v['promotionEvidence'][slot][field]=1;self.assertEqual(a.archive_promotion_record(v)['state'],'invalid-fields')
 def test_proof_flags_cannot_be_promoted(self):
  for field in ['replacementRequested','currentPathAbsenceProven','uniqueFinalZipProven']:
   v=self.attempt();v['promotionEvidence'][field]=True;self.assertEqual(a.archive_promotion_record(v)['state'],'invalid-fields')
 def test_missing_return_fields_are_not_filled_with_null(self):
  for slot,field in [('promotion','accepted'),('promotion','win32Error'),('postPromotionValidation','returned'),('postPromotionValidation','readableAndNonempty')]:
   v=self.attempt();v['promotionEvidence'][slot].pop(field);self.assertEqual(a.archive_promotion_record(v)['state'],'invalid-fields')
 def test_old_cleanup_views_are_independent(self):
  v=self.attempt(True,True,0,True,True,False);before=(a.archive_cleanup_record(v),a.archive_cleanup_return_record(v));a.archive_promotion_record(v);self.assertEqual(before,(a.archive_cleanup_record(v),a.archive_cleanup_return_record(v)))
 def test_timing_reader_adds_separate_view(self):
  v=self.attempt(True,True,0,True,True,False);v.update(attempt=1,attemptBodyReturned=True,totalElapsedMs=1,completedStepElapsedMs={'stagingPreparation':1});d=dict(archiveAttemptTiming=dict(schemaVersion=1,scope='current-archive-call-completed-step-wall-ms',clock='GetTickCount64',maxAttempts=2,maxPhases=8,attemptsObserved=1,attemptTotalIncludesRetryDelay=True,includesArchiveSetupOutsideAttempts=False,originalResultOrDurabilityProven=False,attempts=[v]));r=a.archive_attempt_timing_record(d);self.assertEqual(r['attempts'][0]['promotion'],a.archive_promotion_record(v));self.assertEqual(r['attempts'][0]['cleanupReturn'],a.archive_cleanup_return_record(v))
if __name__=='__main__':unittest.main()
