from pathlib import Path
import copy,json,sys,unittest
sys.path.insert(0,str(Path(__file__).resolve().parent));import log_lane_audit as a
class ArchiveCleanupReturnTests(unittest.TestCase):
 names=('prepare-temporary','prepare-staging','promoted-staging','failed-temporary','failed-staging')
 def attempt(self,new=True):
  sites=[]
  for name in self.names:
   row=dict(site=name,attempted=False,errorValue=None,errorCategory=None)
   if new:row.update(removalReturnSchema=1,removalReturnObserved=False,removalApi=None,removed=None,removeAllRawReturn=None,removeAllCountValid=None)
   sites.append(row)
  return dict(cleanupEvidence=dict(schema=1,scope='original-five-filesystem-cleanup-sites;not-path-absence-or-resource-return',maxSites=5,pathAbsenceProven=False,resourceRetirementProven=False,sites=sites))
 def set_site(self,attempt,name,api,returned,code=0,valid=None):
  row=next(x for x in attempt['cleanupEvidence']['sites'] if x['site']==name);row.update(attempted=True,errorValue=code,errorCategory='system',removalReturnObserved=True,removalApi=api,removed=returned if api=='remove' else None,removeAllRawReturn=returned if api=='remove_all' else None,removeAllCountValid=valid if api=='remove_all' else None);return row
 def test_legacy_remains_missing(self):
  r=a.archive_cleanup_return_record(self.attempt(False));self.assertEqual(r['state'],'legacy-return-fields-missing');self.assertTrue(all(x['state']=='missing' and not x['reported'] for x in r['sites']))
 def test_unentered_fields_stay_null_and_not_return_zero(self):
  r=a.archive_cleanup_return_record(self.attempt());self.assertEqual(r['state'],'reported-original-cleanup-returns');self.assertTrue(all(x['state']=='not-entered' and not x['reported'] for x in r['sites']))
 def test_remove_boolean_false_and_true(self):
  for value in (False,True):
   v=self.attempt();self.set_site(v,'prepare-temporary','remove',value);r=a.archive_cleanup_return_record(v);self.assertEqual(r['sites'][0]['reported']['removed'],value);self.assertFalse(r['pathAbsenceProven'])
 def test_tree_zero_and_positive_counts(self):
  for value in (0,7):
   v=self.attempt();self.set_site(v,'prepare-staging','remove_all',value,valid=True);r=a.archive_cleanup_return_record(v);self.assertEqual(r['sites'][1]['reported']['removedEntryCount'],value)
 def test_error_sentinel_not_deletion_count(self):
  v=self.attempt();self.set_site(v,'promoted-staging','remove_all',(1<<64)-1,32,False);r=a.archive_cleanup_return_record(v);part=r['sites'][2]['reported'];self.assertEqual(part['rawReturn'],(1<<64)-1);self.assertIsNone(part['removedEntryCount']);self.assertFalse(r['resourceRetirementProven'])
 def test_unsigned_above_js_limit_exact(self):
  v=self.attempt();self.set_site(v,'prepare-staging','remove_all',(1<<63)+17,valid=True);r=a.archive_cleanup_return_record(v);self.assertEqual(r['sites'][1]['reported']['rawReturn'],(1<<63)+17)
 def test_wrong_return_schema(self):
  for bad in (True,2,1.0,'1'):
   v=self.attempt();v['cleanupEvidence']['sites'][0]['removalReturnSchema']=bad;self.assertEqual(a.archive_cleanup_return_record(v)['sites'][0]['state'],'unsupported-return-schema')
 def test_invalid_remove_all_numeric_types(self):
  for bad in (True,-1,0.0,'0',1<<64):
   v=self.attempt();self.set_site(v,'prepare-staging','remove_all',bad,valid=True);self.assertEqual(a.archive_cleanup_return_record(v)['sites'][1]['state'],'invalid-remove-all-return-fields')
 def test_wrong_api_and_missing_field(self):
  v=self.attempt();self.set_site(v,'prepare-staging','remove',False);self.assertEqual(a.archive_cleanup_return_record(v)['sites'][1]['state'],'wrong-api-for-site');v=self.attempt();v['cleanupEvidence']['sites'][0].pop('removed');self.assertEqual(a.archive_cleanup_return_record(v)['sites'][0]['state'],'missing-return-fields')
 def test_false_observed_does_not_accept_results(self):
  v=self.attempt();v['cleanupEvidence']['sites'][0]['removed']=False;self.assertEqual(a.archive_cleanup_return_record(v)['sites'][0]['state'],'conflicting-unobserved-return')
 def test_unentered_cannot_have_actual_return(self):
  v=self.attempt();row=v['cleanupEvidence']['sites'][0];row.update(removalReturnObserved=True,removalApi='remove',removed=False);self.assertEqual(a.archive_cleanup_return_record(v)['sites'][0]['state'],'conflicting-unentered-return')
 def test_error_and_true_removed_conflict(self):
  v=self.attempt();self.set_site(v,'failed-temporary','remove',True,32);self.assertEqual(a.archive_cleanup_return_record(v)['sites'][3]['state'],'conflicting-error-and-removed')
 def test_count_validity_requires_original_error_and_non_sentinel(self):
  for count,code,flag in [(5,32,True),((1<<64)-1,0,True),(5,0,False)]:
   v=self.attempt();self.set_site(v,'prepare-staging','remove_all',count,code,flag);self.assertEqual(a.archive_cleanup_return_record(v)['sites'][1]['state'],'conflicting-count-validity')
 def test_negative_generic_error_kept_by_original_view(self):
  v=self.attempt();row=self.set_site(v,'failed-temporary','remove',False,-5);row['errorCategory']='generic';self.assertEqual(a.archive_cleanup_record(v)['sites'][3]['errorValue'],-5);self.assertEqual(a.archive_cleanup_return_record(v)['sites'][3]['state'],'reported-remove-result')
 def test_original_error_only_observation_is_unknown_return(self):
  v=self.attempt();v['cleanupEvidence']['sites'][0].update(attempted=True,errorValue=0,errorCategory='system');self.assertEqual(a.archive_cleanup_return_record(v)['sites'][0]['state'],'return-not-observed')
 def test_invalid_duplicate_and_large_wrapper_not_inspected(self):
  for v in [None,dict(cleanupEvidence=True)]:self.assertNotEqual(a.archive_cleanup_return_record(v)['state'],'reported-original-cleanup-returns')
  v=self.attempt();v['cleanupEvidence']['sites']*=1000;r=a.archive_cleanup_return_record(v);self.assertEqual(r['state'],'original-cleanup-wrapper-unrecognised');self.assertEqual(r['sites'],[])
 def test_actual_timing_reader_attaches_new_view_separately(self):
  v=self.attempt();self.set_site(v,'prepare-temporary','remove',False);v.update(attempt=1,attemptBodyReturned=True,totalElapsedMs=1,completedStepElapsedMs={'stagingPreparation':1})
  d=dict(archiveAttemptTiming=dict(schemaVersion=1,scope='current-archive-call-completed-step-wall-ms',clock='GetTickCount64',maxAttempts=2,maxPhases=8,attemptsObserved=1,attemptTotalIncludesRetryDelay=True,includesArchiveSetupOutsideAttempts=False,originalResultOrDurabilityProven=False,attempts=[v]))
  r=a.archive_attempt_timing_record(d);self.assertEqual(r['attempts'][0]['cleanup'],a.archive_cleanup_record(v));self.assertEqual(r['attempts'][0]['cleanupReturn']['observedReturnSites'],1)
 def cleanup_candidate_attempt(self):
  v=self.attempt();self.set_site(v,'failed-temporary','remove',False,32)
  self.set_site(v,'failed-staging','remove_all',(1<<64)-1,32,False)
  v.update(attempt=1,attemptBodyReturned=True,totalElapsedMs=2,completedStepElapsedMs={'attemptCleanup':2})
  v['failedTemporaryAfterCleanup']=dict(schema=1,scope='owned-attempt-temporary-after-failed-cleanup;metadata-only',
                                       state='regular-file-metadata',win32Error=0,bytes=323,elapsedMs=1)
  return v
 def residue(self,v):
  return a.archive_failed_temporary_after_cleanup_record(v,a.archive_cleanup_record(v))
 def test_owned_candidate_lookup_keeps_states_separate(self):
  for state,error,size in [('regular-file-metadata',0,323),('regular-file-metadata',0,0),
                           ('not-found-at-lookup',2,None),('not-found-at-lookup',3,None),
                           ('attribute-query-failed',5,None),('non-regular-path',0,None)]:
   v=self.cleanup_candidate_attempt();v['failedTemporaryAfterCleanup'].update(state=state,win32Error=error,bytes=size)
   r=self.residue(v);self.assertEqual(r['state'],'reported-owned-temporary-observation')
   self.assertEqual(r['reported'],dict(state=state,win32Error=error,bytes=size,elapsedMs=1));self.assertFalse(r['currentPathStateOrUniqueZipProven'])
 def test_cleanup_error_is_required_for_candidate_lookup(self):
  v=self.cleanup_candidate_attempt()
  self.set_site(v,'failed-temporary','remove',False);self.set_site(v,'failed-staging','remove_all',0,valid=True)
  self.assertEqual(self.residue(v)['state'],'failed-cleanup-context-unverified')
  v=self.cleanup_candidate_attempt();v['attemptBodyReturned']=False
  self.assertEqual(self.residue(v)['state'],'failed-cleanup-context-unverified')
  v=self.cleanup_candidate_attempt();self.set_site(v,'promoted-staging','remove_all',0,valid=True)
  self.assertEqual(self.residue(v)['state'],'failed-cleanup-context-unverified')
 def test_cleanup_lookup_rejects_invalid_observations_and_keeps_old_missing(self):
  for fields in (dict(bytes=True),dict(elapsedMs=3),dict(win32Error=False),dict(state='not-found-at-lookup',bytes=None,win32Error=5)):
   v=self.cleanup_candidate_attempt();v['failedTemporaryAfterCleanup'].update(fields)
   self.assertEqual(self.residue(v)['state'],'invalid-observation')
  self.assertEqual(self.residue(self.attempt())['state'],'missing')
 def test_cleanup_lookup_is_kept_in_attempt_check_projection(self):
  from log_lane_audit_selftest import ArchiveAttemptTimingTests
  d=ArchiveAttemptTimingTests.receipt();d['attempts']=[self.cleanup_candidate_attempt()];d['attemptsObserved']=1
  timing,checks,_=a.cross_archive_terminal_details(dict(archiveAttemptTiming=d))
  self.assertEqual(timing['state'],'reported-completed-steps')
  self.assertEqual(checks['attempts'][0]['failedTemporaryAfterCleanup'],self.residue(d['attempts'][0]))
if __name__=='__main__':unittest.main()
