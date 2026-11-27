"""Offline ordinary-export operation associations;never run the product."""
from pathlib import Path
import copy,json,sys,tempfile,unittest,warnings,zipfile
sys.path.insert(0,str(Path(__file__).resolve().parent))
import log_lane_audit as a
class OrdinaryExportTransactionTests(unittest.TestCase):
 def test_explicit_quick_skip_and_conflicts(self):
  v=self.availability(); q=dict(schema='ymcc.sleep-export-diagnostics.v1',status='NOT_COLLECTED',reason='skipped-by-request',reportAvailable=False,queryAttempts=0)
  v['windowsSleepDiagnostics']=q
  current=self.view([],v)['windowsQuery'];self.assertEqual(current['state'],'reported-skipped-by-request');self.assertEqual(current['query'],{});self.assertFalse(current['currentReportOrSleepSuccessInferred'])
  for fields in ({'queryAttempts':False},{'query':{}},{'reportAvailable':True},{'recentDeadline':{}},{'querySuppressed':False},{'status':'UNKNOWN'}):
   v['windowsSleepDiagnostics']=dict(q,**fields);self.assertEqual(self.view([],v)['windowsQuery']['state'],'invalid-fields')
 def operation(self,birth='134394066260992255',ordinal='1'):
  return dict(schema=1,scope='admitted-ordinary-log-export;diagnostic-correlation-only',id='7012-10256843-'+ordinal,
              writerPid=7012,writerProcessCreation100ns=birth,ordinal=ordinal,startedTickMs='10256843')
 def availability(self):
  return dict(exportOperation=self.operation(),windowsSleepDiagnostics=dict(status='UNKNOWN',reason='query-not-completed-successfully',queryAttempts=1,timeoutMs=10000,reportAvailable=False,
              query=dict(ran=True,completion='deadline',elapsedMs=10015,exitCode=1460,win32Error=0)),exportPhaseTiming=dict(elapsedMs={'flush':0,'windowsSystemPowerReport':10015}))
 def terminal(self):
  return dict(event='log-export-result',pid=7012,writerProcessCreation100ns='134394066260992255',exportOperation=self.operation(),
              ok=True,cleanupOk=False,attempts=2,bodyReturned=True,terminalStage='archive-return',reason='ok',completion='exited',exportElapsedMs=14600,archiveElapsedMs=4203,
              phaseElapsedMs={'flush':0,'windowsSystemPowerReport':10015,'snapshotIndexAndArchive':4203},phaseTimingBuildFailures=0)
 def view(self,rows=None,availability=None,selection=None,filename='native-lifecycle.log'):
  if rows is None:rows=[self.terminal()]
  return a.ordinary_export_transaction_evidence(self.availability() if availability is None else availability,
      {'status':'selected','selectedOccurrence':0} if selection is None else selection,
      [(filename,[(i+1,row) for i,row in enumerate(rows)],{'archiveEntryOccurrence':0})])
 def test_overlapped_windows_timing_separates_combined_wall_and_adapter(self):
  v=self.availability();v['exportPhaseTiming']['elapsedMs']={'windowsReportAndRemainingState':10021,'manifestWrite':87}
  self.assertEqual(self.view([],v)['windowsSleepScheduling']['state'],'missing')
  v['windowsSleepScheduling']=dict(schema=1,scope='export-wall-intervals;may-overlap',mode='overlapped-task',
      reportAdapterMs=10018,remainingStateMs=150,overlapMs=149,joinMs=9868)
  row=self.terminal();row['phaseElapsedMs']=dict(v['exportPhaseTiming']['elapsedMs'])
  r=self.view([row],v);s=r['windowsSleepScheduling'];self.assertEqual(s['state'],'reported-overlap-intervals')
  self.assertEqual(s['reported']['reportAdapterMs'],10018);self.assertEqual(s['reported']['overlapMs'],149)
  self.assertEqual(r['terminal']['terminal']['phaseTiming']['elapsedMs'],row['phaseElapsedMs'])
  self.assertIsNone(r['terminal']['terminal']['windowsPhaseFractionOfExport'])
  for change in ({'overlapMs':151},{'mode':'inline-launch-fallback'},{'remainingStateMs':True},{'joinMs':10000}):
   wrong=dict(v,windowsSleepScheduling=dict(v['windowsSleepScheduling'],**change))
   self.assertEqual(self.view([],wrong)['windowsSleepScheduling']['state'],'invalid-fields')
 def test_manifest_identity_is_contained_in_remaining_state_time(self):
  v=self.availability();v['exportPhaseTiming']['elapsedMs']={'windowsReportAndRemainingState':10000,'manifestWrite':67}
  v['windowsSleepScheduling']=dict(schema=1,scope='export-wall-intervals;may-overlap',mode='overlapped-task',
      reportAdapterMs=10000,remainingStateMs=212,overlapMs=212,joinMs=9788)
  legacy=a.windows_query_scheduling_cohort_record(self.view([],v))
  self.assertNotIn('manifestNativeIdentity',legacy['intervals'])
  for valid in (dict(elapsedMs=40,overlapMs=40),dict(elapsedMs=0,overlapMs=0)):
   v['windowsSleepScheduling']['manifestNativeIdentity']=valid
   parsed=a.windows_query_scheduling_cohort_record(self.view([],v))
   self.assertEqual(parsed,dict(legacy,intervals=dict(legacy['intervals'],manifestNativeIdentity=valid)))
  for invalid in (None,{},dict(elapsedMs=40,overlapMs=39),dict(elapsedMs=30,overlapMs=31),
                  dict(elapsedMs=213,overlapMs=213),dict(elapsedMs=True,overlapMs=0)):
   v['windowsSleepScheduling']['manifestNativeIdentity']=invalid
   parsed=a.windows_query_scheduling_cohort_record(self.view([],v))
   self.assertEqual(parsed['state'],'invalid-fields');self.assertIsNone(parsed['intervals'])
   self.assertEqual(parsed['combinedWallMs'],10000);self.assertEqual(parsed['manifestWriteMs'],67)
 def test_unique_complete_operation(self):
  r=self.view();self.assertEqual(r['state'],'unique-reported-terminal-row');self.assertTrue(r['terminalRowsPaired']);self.assertTrue(r['terminal']['terminal']['reported']['ok']);self.assertFalse(r['currentIpcOrContainingZipOutcomeProven'])
 def test_exact_birth_above_js_safe_integer(self):
  r=self.view();self.assertEqual(r['operation']['reported']['writerProcessCreation100ns'],'134394066260992255');self.assertFalse(r['osWriterIdentityProven'])
 def test_no_terminal_is_unknown_not_previous_success(self):
  row=self.terminal();row['exportOperation']=self.operation(ordinal='2');r=self.view([row]);self.assertEqual(r['state'],'terminal-not-observed');self.assertIsNone(r['terminal']);self.assertEqual(r['eligibleMatchingRows'],0)
 def test_preceding_ordinal_has_its_own_terminal_and_timing(self):
  current=self.availability();current['exportOperation'].update(id='7012-10458843-2',ordinal='2',startedTickMs='10458843')
  row=self.terminal();r=self.view([row],current);p=r['precedingOperation']
  self.assertEqual(r['state'],'terminal-not-observed');self.assertIsNone(r['terminal'])
  self.assertEqual(p['state'],'unique-reported-preceding-terminal');self.assertEqual(p['terminal']['startTickDeltaMs'],202000)
  self.assertEqual(p['terminal']['terminal']['reported']['exportElapsedMs'],14600);self.assertFalse(p['currentExportOutcomeInferred'])
  self.assertFalse(p['uniqueFinalFileOrDurableCleanupProven'])
 def test_preceding_ordinal_refuses_stale_or_ambiguous_rows(self):
  current=self.availability();current['exportOperation'].update(id='7012-10458843-2',ordinal='2',startedTickMs='10458843')
  row=self.terminal(); reused=copy.deepcopy(row);reused['exportOperation']['writerProcessCreation100ns']='134394066260992256'
  bad_outer=dict(row,writerProcessCreation100ns=None)
  later=copy.deepcopy(row);later['exportOperation'].update(id='7012-10500000-1',startedTickMs='10500000')
  unsupported=copy.deepcopy(row);unsupported['exportOperation']['extra']=1
  for rows,state in [([reused],'terminal-not-observed'),([bad_outer],'conflicting-or-incomplete-terminal'),([later],'conflicting-or-incomplete-terminal'),([unsupported],'conflicting-or-incomplete-terminal'),([row,row],'ambiguous-retained-terminals'),([row,bad_outer],'ambiguous-retained-terminals')]:
   with self.subTest(state=state):
    r=self.view(rows,current);p=r['precedingOperation'];self.assertEqual(p['state'],state);self.assertIsNone(p['terminal']);self.assertIsNone(r['terminal'])
 def test_same_id_different_birth_not_joined(self):
  row=self.terminal();row['exportOperation']=self.operation(birth='134394066260992256');r=self.view([row]);self.assertEqual(r['sameIdDifferentOperationRows'],1);self.assertFalse(r['terminalRowsPaired'])
 def test_outer_pid_conflict(self):
  row=self.terminal();row['pid']=42;r=self.view([row]);self.assertEqual(r['state'],'matching-operation-writer-conflict');self.assertIsNone(r['terminal'])
 def test_outer_birth_conflict(self):
  row=self.terminal();row['writerProcessCreation100ns']='134394066260992256';self.assertEqual(self.view([row])['state'],'matching-operation-writer-conflict')
 def test_missing_or_bad_outer_identity(self):
  for key,value in [('pid',None),('pid',True),('writerProcessCreation100ns',None),('writerProcessCreation100ns',134394066260992255),('writerProcessCreation100ns','001')]:
   row=self.terminal();row[key]=value;r=self.view([row]);self.assertEqual(r['state'],'matching-operation-writer-unavailable');self.assertFalse(r['terminalRowsPaired'])
 def test_birth_unknown_operation_not_joinable(self):
  v=self.availability();v['exportOperation']['writerProcessCreation100ns']=None;r=self.view(availability=v);self.assertEqual(r['state'],'operation-birth-unknown');self.assertIsNone(r['exactOperationRows'])
 def test_missing_null_and_invalid_operation(self):
  for value in (None,False,{},dict(schema=True)):
   v=self.availability();v['exportOperation']=value;r=self.view(availability=v);self.assertEqual(r['state'],'operation-unusable');self.assertFalse(r['terminalRowsPaired'])
  v=self.availability();v.pop('exportOperation');self.assertEqual(self.view(availability=v)['state'],'operation-unusable')
 def test_unknown_operation_fields_are_not_silently_ignored(self):
  v=self.availability();v['exportOperation']['newField']=1;r=self.view(availability=v);self.assertEqual(r['state'],'operation-unsupported-fields')
 def test_candidate_bool_schema_cannot_equal_integer_schema(self):
  row=self.terminal();row['exportOperation']['schema']=True;r=self.view([row]);self.assertEqual(r['state'],'terminal-not-observed');self.assertEqual(r['sameIdDifferentOperationRows'],1)
 def test_noncanonical_operation_component(self):
  for key,value in [('ordinal','01'),('startedTickMs',10256843),('writerProcessCreation100ns','0134394066260992255'),('id','other')]:
   v=self.availability();v['exportOperation'][key]=value;self.assertEqual(self.view(availability=v)['state'],'operation-unusable')
 def test_ambiguous_or_missing_availability_never_joined(self):
  for status in ('missing','ambiguous','duplicate','parse-failed'):
   r=self.view(selection={'status':status,'selectedOccurrence':None});self.assertEqual(r['state'],'receipt-unavailable');self.assertIsNone(r['retainedTerminalRowsInspected'])
 def test_repeated_identical_terminal_stays_multiple(self):
  row=self.terminal();r=self.view([row,copy.deepcopy(row)]);self.assertEqual(r['state'],'multiple-matching-operation-rows');self.assertFalse(r['differentMatchingProjectionObserved']);self.assertIsNone(r['terminal']);self.assertFalse(r['terminalRowsPaired'])
 def test_conflicting_terminal_stays_multiple(self):
  row=self.terminal();other=copy.deepcopy(row);other['ok']=False;r=self.view([row,other]);self.assertTrue(r['differentMatchingProjectionObserved']);self.assertIsNone(r['terminal'])
 def test_one_good_plus_wrong_writer_not_unique(self):
  row=self.terminal();other=copy.deepcopy(row);other['pid']=42;r=self.view([row,other]);self.assertEqual(r['state'],'multiple-matching-operation-rows');self.assertEqual(r['eligibleMatchingRows'],1);self.assertEqual(r['writerConflictingExactOperationRows'],1);self.assertFalse(r['terminalRowsPaired'])
 def test_all_matches_counted_with_only_sixteen_witnesses(self):
  r=self.view([self.terminal() for _ in range(10000)]);self.assertEqual(r['exactOperationRows'],10000);self.assertEqual(len(r['matchingWitnesses']),16);self.assertEqual(r['omittedMatchingWitnesses'],9984);self.assertEqual(r['matchingWitnesses'][0]['line'],1);self.assertEqual(r['matchingWitnesses'][-1]['line'],10000)
 def test_many_unrelated_rows_do_not_occupy_matching_capacity(self):
  rows=[dict(event='other') for _ in range(10000)]+[self.terminal()];r=self.view(rows);self.assertEqual(r['state'],'unique-reported-terminal-row');self.assertEqual(r['retainedTerminalRowsInspected'],1)
 def test_external_and_unrecognised_carriers_not_joined(self):
  for name in ('steam/native-lifecycle.log','app.log','native-lifecycle.log.extra'):
   self.assertEqual(self.view(filename=name)['exactOperationRows'],0)
 def test_native_rotations_qualify(self):
  for name in ('native-lifecycle.log.1','native-detail.log.prev','native-lifecycle.log.previous'):
   self.assertEqual(self.view(filename=name)['state'],'unique-reported-terminal-row')
 def test_zero_phase_missing_total_and_bad_types(self):
  row=self.terminal();row['phaseElapsedMs']['flush']=0;row['exportElapsedMs']=True;r=self.view([row])['terminal']['terminal'];self.assertEqual(r['phaseTiming']['elapsedMs']['flush'],0);self.assertIsNone(r['windowsPhaseFractionOfExport']);self.assertIn('exportElapsedMs:invalid-type-range-or-length',r['fieldIssues'])
 def test_query_timeout_zip_success_and_cleanup_failure_independent(self):
  r=self.view();self.assertEqual(r['windowsQuery']['query']['exitCode'],1460);self.assertEqual(r['windowsQuery']['query']['elapsedMs'],10015);self.assertEqual(r['terminal']['terminal']['phaseTiming']['elapsedMs']['windowsSystemPowerReport'],10015);self.assertTrue(r['terminal']['terminal']['reported']['ok']);self.assertFalse(r['terminal']['terminal']['reported']['cleanupOk']);self.assertFalse(r['querySuccessFromZipOkInferred']);self.assertFalse(r['uniqueFinalFileOrDurableCleanupProven'])
 def test_query_inner_and_phase_are_not_added(self):
  v=self.availability();v['windowsSleepDiagnostics']['query']['elapsedMs']=10000;r=self.view(availability=v);self.assertEqual(r['windowsQuery']['query']['elapsedMs'],10000);self.assertEqual(r['terminal']['terminal']['windowsPhaseFractionOfExport'],{'numeratorMs':10015,'denominatorMs':14600});self.assertFalse(r['queryInnerElapsedAddedToPhase'])
 def test_phase_larger_than_total_no_fraction(self):
  row=self.terminal();row['exportElapsedMs']=1;r=self.view([row])['terminal']['terminal'];self.assertIsNone(r['windowsPhaseFractionOfExport']);self.assertIn('windows-phase:exceeds-reported-export-wall',r['fieldIssues'])
 def test_terminal_stage_not_inferred_from_ok(self):
  row=self.terminal();row.pop('terminalStage');row.pop('bodyReturned');r=self.view([row])['terminal']['terminal'];self.assertEqual(r['terminalStageState'],'not-observed');self.assertTrue(r['reported']['ok'])
 def test_body_exception_stage_kept_separate(self):
  row=self.terminal();row.update(terminalStage='body-exception-after-archive',bodyReturned=False,ok=True);r=self.view([row])['terminal']['terminal'];self.assertEqual(r['terminalStageState'],'coherent-reported-stage');self.assertTrue(r['reported']['ok']);self.assertFalse(r['reported']['bodyReturned'])
 def test_real_archive_audit_integration(self):
  source=(Path(__file__).resolve().parents[1]/'native/main.cpp').read_text('utf-8-sig')
  with tempfile.TemporaryDirectory(prefix='ymcc-export-operation-') as d:
   p=Path(d)/'example.zip'
   with zipfile.ZipFile(p,'w') as z:z.writestr('log-export-availability.json',json.dumps(self.availability()));z.writestr('native-lifecycle.log.1',json.dumps(self.terminal())+'\n')
   r=a.audit_zip(p,a.source_policy(source))
  self.assertEqual(r['commonEvidence']['ordinaryExportTransaction']['state'],'unique-reported-terminal-row');self.assertFalse(r['commonEvidence']['exportOperationEvidence']['terminalRowsPaired'])
 def test_archive_preserves_prior_and_exact_current_separately(self):
  source=(Path(__file__).resolve().parents[1]/'native/main.cpp').read_text('utf-8-sig')
  current=self.availability();current['exportOperation'].update(id='7012-10458843-2',ordinal='2',startedTickMs='10458843')
  prior=self.terminal();terminal=dict(prior,exportOperation=current['exportOperation'],ok=False)
  with tempfile.TemporaryDirectory(prefix='ymcc-export-two-operations-') as d:
   p=Path(d)/'example.zip'
   with zipfile.ZipFile(p,'w') as z:
    z.writestr('log-export-availability.json',json.dumps(current));z.writestr('native-lifecycle.log.1',json.dumps(prior)+'\n');z.writestr('native-lifecycle.log',json.dumps(terminal)+'\n')
   view=a.audit_zip(p,a.source_policy(source))['commonEvidence']['ordinaryExportTransaction']
  self.assertEqual(view['state'],'unique-reported-terminal-row');self.assertFalse(view['terminal']['terminal']['reported']['ok'])
  self.assertTrue(view['precedingOperation']['terminal']['terminal']['reported']['ok'])
 def carrying_availability(self):
  current=self.availability();current['exportOperation'].update(id='7012-10458843-2',ordinal='2',startedTickMs='10458843')
  raw=dict(self.terminal(),exportMode='full',win32Error=0)
  compact={k:v for k,v in raw.items() if k not in ('event','pid','writerProcessCreation100ns','exportOperation')}
  current['priorOrdinaryExportTerminal']=dict(schema=1,exportOperation=raw['exportOperation'],terminal=dict(compact,bodyResultOk=True))
  return current,raw
 def copy_failure_wrapper(self, lost=0, both=False):
  copy_observed=dict(schema=1,scope='original-shared-copy-calls-only',stage='destination-write',
      sourceSizeKnown=True,sourceBytesAtOpen=48,successfulReadBytes=48,successfulWriteBytes=12,
      readCalls=1,writeCalls=1,destinationCreated=True,copyComplete=False,sourceStabilityProven=False,
      durableWriteProven=False,resourceReturnProven=False)
  detail=dict(attempt=1,listIndex=3,entry='native-lifecycle.log',win32Error=112,copyObservation=copy_observed)
  rows=[detail,dict(detail,attempt=2,win32Error=5)] if both else [detail]
  observed=len(rows); rows=rows[:observed-lost]
  return dict(schema=1,scope='mandatory-shared-copy-failures-in-this-export-call',failureObservations=observed,
      evidenceBuildFailures=lost,retainedFailures=len(rows),maxRetainedFailures=2,
      retainedObservedFailuresComplete=not lost,sourcePathsOmitted=True,
      sourceStabilityDurabilityOrResourceReturnProven=False,failures=rows or None)
 def test_copy_failure_carry_observations_do_not_change_core_success(self):
  current,row=self.carrying_availability(); carry=current['priorOrdinaryExportTerminal']
  field='snapshotCopyFailureEvidence'; self.assertNotIn(field,self.view([],current)['precedingOperationCarry'])
  full=self.copy_failure_wrapper(); carry[field]=full
  def read(rows=()):return self.view(rows,current)['precedingOperationCarry']
  detail=read()[field]; self.assertEqual(detail['state'],a.PRIOR_COPY_FAILURE_STATES[0]); self.assertTrue(read()['terminal']['reported']['ok'])
  self.assertEqual(detail['retainedRowComparison'],'terminal-not-observed')
  inner=detail['value']['failures'][0]; self.assertEqual(inner['reported']['win32Error'],112)
  self.assertEqual(inner['copyObservation']['reported']['successfulWriteBytes'],12)
  for raw,expected in ((row,'raw-copy-failure-not-observed'),(dict(row,**{field:full}),'reported-fields-agree'),
                       (dict(row,**{field:dict(full,failures=[dict(full['failures'][0],win32Error=5)])}),'reported-fields-conflict')):
   with self.subTest(expected=expected):self.assertEqual(read([raw])[field]['retainedRowComparison'],expected)
  lost=self.copy_failure_wrapper(lost=1); carry[field]=lost
  detail=read()[field]; self.assertEqual(detail['state'],a.PRIOR_COPY_FAILURE_STATES[1]); self.assertEqual(detail['value']['failures'],[])
  self.assertEqual(detail['value']['reported']['failureObservations'],1); self.assertEqual(detail['value']['reported']['evidenceBuildFailures'],1)
  impossible=self.copy_failure_wrapper(lost=2,both=True); carry[field]=impossible
  self.assertEqual(read()[field]['state'],'contradictory');self.assertTrue(read()['terminal']['reported']['ok'])
  malformed=copy.deepcopy(full); malformed['failures'][0]['copyObservation']['sourcePath']='hidden'
  carry[field]=malformed; self.assertEqual(read()[field]['state'],'invalid-fields'); self.assertTrue(read()['terminal']['reported']['ok'])
 def test_copy_failure_carry_double_failure_and_byte_limit(self):
  current,row=self.carrying_availability(); carry=current['priorOrdinaryExportTerminal']
  field='snapshotCopyFailureEvidence'; carry['terminal'].update(ok=False,bodyResultOk=False,reason='copy-failed',win32Error=5)
  row.update(ok=False,reason='copy-failed',win32Error=5)
  two=self.copy_failure_wrapper(both=True); carry[field]=two; row[field]=copy.deepcopy(two)
  detail=self.view([row],current)['precedingOperationCarry'][field]
  self.assertEqual(detail['retainedRowComparison'],'reported-fields-agree')
  self.assertEqual([item['reported']['attempt'] for item in detail['value']['failures']],[1,2])
  over=copy.deepcopy(two)
  for failure in over['failures']:
   failure['entry']='\x01'*256
   for key in ('listIndex',):failure[key]=(1<<64)-1
  carry[field]=over
  base=self.view([row],current)['precedingOperationCarry']
  self.assertEqual(base[field]['state'],'byte-limit-exceeded');self.assertIsNone(base[field]['value'])
  self.assertEqual(base['retainedRowComparison'],'reported-fields-agree')
 def test_prior_carry_is_separate_when_raw_absent_or_old(self):
  current,_=self.carrying_availability();r=self.view([],current);carry=r['precedingOperationCarry']
  self.assertEqual(carry['state'],'reported-in-memory-preceding-terminal')
  self.assertEqual(carry['retainedRowComparison'],'terminal-not-observed')
  self.assertEqual(carry['terminal']['reported']['attempts'],2);self.assertTrue(carry['bodyResultOk'])
  self.assertFalse(carry['originalLogRow']);self.assertFalse(carry['currentExportOutcomeInferred'])
  self.assertFalse(carry['uniqueFinalFileOrDurableCleanupProven']);self.assertFalse(r['terminalRowsPaired'])
  self.assertEqual(r['retainedTerminalRowsInspected'],0);self.assertEqual(r['precedingOperation']['candidateRows'],0)
  self.assertIsNone(r['terminal']);self.assertIsNone(r['precedingOperation']['terminal'])
  cross,_=self.cross_view([(current,[])]);self.assertEqual(cross['operationsIndexed'],0)
  current.pop('priorOrdinaryExportTerminal')
  self.assertEqual(self.view([],current)['precedingOperationCarry']['state'],'not-observed')
 def test_renderer_reply_preserves_separate_elapsed_and_prior_result(self):
  current,row=self.carrying_availability();original=current['priorOrdinaryExportTerminal']
  self.assertNotIn('rendererReply',self.view([row],current)['precedingOperationCarry'])
  original['rendererReply']=dict(schema=1,replyOk=True,elapsedMs=15123)
  r=self.view([row],current);carry=r['precedingOperationCarry']
  self.assertEqual(carry['rendererReply']['state'],'reported-matching-preceding-renderer-reply')
  self.assertEqual(carry['rendererReply']['elapsedMs'],15123)
  self.assertEqual(carry['terminal']['reported']['exportElapsedMs'],14600)
  self.assertEqual(carry['retainedRowComparison'],'reported-fields-agree');self.assertIsNone(r['terminal'])
  original['terminal']['bodyResultOk']=False
  original['rendererReply']=dict(schema=1,replyOk=False,elapsedMs=0)
  r=self.view([],current)['precedingOperationCarry']['rendererReply']
  self.assertEqual(r['state'],'reported-matching-preceding-renderer-reply');self.assertEqual(r['elapsedMs'],0);self.assertFalse(r['replyOk'])
 def test_bad_renderer_reply_does_not_suppress_native_carry(self):
  current,row=self.carrying_availability();original=current['priorOrdinaryExportTerminal']
  valid=dict(schema=1,replyOk=True,elapsedMs=15123)
  for bad in (None,dict(valid,schema=True),dict(valid,replyOk=1),dict(valid,elapsedMs=True),
              dict(valid,elapsedMs=-1),dict(valid,elapsedMs=1<<53),dict(valid,file='must-not-be-carried'),dict(valid,replyOk=False)):
   with self.subTest(bad=bad):
    original['rendererReply']=bad; carry=self.view([row],current)['precedingOperationCarry']
    self.assertEqual(carry['retainedRowComparison'],'reported-fields-agree')
    self.assertEqual(carry['rendererReply']['state'],'preceding-return-mismatch' if bad==dict(valid,replyOk=False) else 'invalid-fields')
    self.assertIsNone(carry['rendererReply']['elapsedMs']);self.assertIsNone(carry['rendererReply']['replyOk'])
  initial=copy.deepcopy(current);initial['priorOrdinaryExportTerminal']['exportOperation']['writerProcessCreation100ns']='134394066260992256'
  carry=self.view([row],initial)['precedingOperationCarry'];self.assertEqual(carry['state'],'preceding-operation-unverified');self.assertNotIn('rendererReply',carry)
 def test_prior_carry_and_raw_agreement_conflict_and_ambiguity(self):
  current,row=self.carrying_availability()
  for rows,comparison,count in (([row],'reported-fields-agree',1),
     ([dict(row,cleanupOk=True)],'reported-fields-conflict',1),
     ([dict(row,phaseElapsedMs=dict(row['phaseElapsedMs'],flush=True))],'reported-fields-conflict',1),
     ([row,row],'ambiguous-retained-terminals',2),([dict(row,pid=42)],'conflicting-or-incomplete-terminal',1)):
   with self.subTest(comparison=comparison):
    r=self.view(rows,current);carry=r['precedingOperationCarry']
    self.assertEqual(carry['state'],'reported-in-memory-preceding-terminal');self.assertEqual(carry['retainedRowComparison'],comparison)
    self.assertEqual(r['precedingOperation']['candidateRows'],count);self.assertIsNone(r['terminal'])
 def test_prior_carry_requires_complete_immediately_preceding_identity(self):
  current,_=self.carrying_availability()
  for fields in (dict(writerPid=7013,id='7013-10256843-1'),dict(writerProcessCreation100ns=None),
       dict(writerProcessCreation100ns='134394066260992256'),dict(startedTickMs='10458843',id='7012-10458843-1'),
       dict(ordinal='0',id='7012-10256843-0'),dict(scope='other'),dict(unrecognised=True)):
   v=copy.deepcopy(current);v['priorOrdinaryExportTerminal']['exportOperation'].update(fields)
   self.assertEqual(self.view([],v)['precedingOperationCarry']['state'],'preceding-operation-unverified')
  current['exportOperation'].update(ordinal='3',id='7012-10458843-3')
  self.assertEqual(self.view([],current)['precedingOperationCarry']['state'],'preceding-operation-unverified')
 def test_prior_carry_retry_detail_is_bound_to_first_failed_attempt(self):
  current,row=self.carrying_availability(); carried=current['priorOrdinaryExportTerminal']
  retry=dict(schema=1,scope='previous-attempt-only;not-current-export-outcome',attempt=1,
             reason='promote-failed',win32Error=32,temporaryRemoveError=0,stagingRemoveError=32,cleanupOk=False)
  carried['previousArchiveAttempt']=retry
  read=self.view([],current)
  previous=read['precedingOperationCarry']['previousArchiveAttempt']
  self.assertEqual(previous['state'],'reported-previous-failed-attempt');self.assertEqual(previous['reported']['win32Error'],32)
  self.assertEqual(previous['reported']['stagingRemoveError'],32);self.assertIsNone(read['terminal'])
  for original,expected in ((retry,'reported-fields-agree'),(dict(retry,win32Error=5),'reported-fields-conflict'),
                           (None,'raw-retry-not-observed'),(dict(retry,win32Error=True),'raw-retry-unusable')):
   view=self.view([dict(row,previousArchiveAttempt=original)],current)
   self.assertEqual(view['precedingOperationCarry']['previousArchiveAttempt']['retainedRowComparison'],expected)
  for malformed,expected in ((dict(retry,attempt=2),'invalid-fields'),(dict(retry,cleanupOk=True),'invalid-fields'),
                             (dict(retry,win32Error=True),'invalid-fields'),(dict(retry,other=1),'invalid-fields'),(None,'null')):
   carried['previousArchiveAttempt']=malformed
   view=self.view([],current)['precedingOperationCarry']
   self.assertEqual(view['state'],'reported-in-memory-preceding-terminal')
   self.assertEqual(view['previousArchiveAttempt']['state'],expected);self.assertEqual(view['previousArchiveAttempt']['reported'],{})
  carried['previousArchiveAttempt']=retry;carried['terminal']['attempts']=1
  self.assertEqual(self.view([],current)['precedingOperationCarry']['previousArchiveAttempt']['state'],'unexpected-attempt-count')
 def test_prior_carry_return_and_exception_do_not_inherit_archive_ok(self):
  current,_=self.carrying_availability();carry=current['priorOrdinaryExportTerminal']
  compact=carry['terminal'];compact.update(terminalStage='body-exception-after-archive',bodyReturned=False,exceptionClass='std-exception')
  compact.pop('bodyResultOk')
  previous=self.view([],current)['precedingOperationCarry']
  self.assertEqual(previous['state'],'reported-in-memory-preceding-terminal')
  self.assertIsNone(previous['bodyResultOk']);self.assertTrue(previous['terminal']['reported']['ok'])
  for fields in (dict(bodyResultOk=True),dict(win32Error=False),dict(attempts=True),dict(archiveAttemptTiming={}),dict(phaseElapsedMs={'unknown':1})):
   carry['terminal']=dict(compact,**fields)
   self.assertEqual(self.view([],current)['precedingOperationCarry']['state'],'invalid-terminal')
  carry['terminal']=dict(ok=False,bodyReturned=True,attempts=0,exportElapsedMs=1,phaseTimingBuildFailures=0,
       terminalStage='before-archive-return',exportMode='full',bodyResultOk=False,reason='no-desktop')
  self.assertIs(self.view([],current)['precedingOperationCarry']['bodyResultOk'],False)
 def test_prior_final_output_is_independent_of_current_and_rejects_bad_shapes(self):
  current,row=self.carrying_availability();carry=current['priorOrdinaryExportTerminal']
  final=dict(schema=1,attempt=2,state='stream-opened-nonempty',sizeBytes=1234)
  carry['finalOutputAfterMove']=final
  view=self.view([],current);output=view['precedingOperationCarry']['finalOutputAfterMove']
  self.assertEqual(output['reported'],final);self.assertEqual(output['retainedRowComparison'],'terminal-not-observed')
  self.assertFalse(output['uniqueFinalFileOrDurableCleanupProven']);self.assertIsNone(view['terminal'])
  self.assertEqual(self.view([row],current)['precedingOperationCarry']['finalOutputAfterMove']['retainedRowComparison'],'raw-final-check-not-observed')
  for replacement,expected in ((None,'null'),(dict(final,schema=True),'unsupported-schema'),
      (dict(final,attempt=1),'unexpected-attempt'),(dict(final,sizeBytes=True),'invalid-fields'),
      (dict(final,openWin32Error=32),'invalid-fields'),(dict(final,state='not-entered'),'invalid-fields'),
      (dict(final,extra='file path'),'invalid-fields')):
   carry['finalOutputAfterMove']=replacement
   prior=self.view([],current)['precedingOperationCarry']
   self.assertEqual(prior['state'],'reported-in-memory-preceding-terminal')
   self.assertEqual(prior['finalOutputAfterMove']['state'],expected)
   self.assertEqual(prior['finalOutputAfterMove']['reported'],{})
 def test_prior_final_output_compares_with_exact_original_failed_open(self):
  current,row=self.carrying_availability();carry=current['priorOrdinaryExportTerminal']
  carry['terminal'].update(ok=False,bodyResultOk=False);row.update(ok=False)
  final=dict(schema=1,attempt=2,state='stream-open-failed',sizeBytes=1234,openWin32Error=32)
  carry['finalOutputAfterMove']=final
  promotion=dict(schema=1,scope='original-MoveFileExW-and-post-promotion-validation;not-current-path-or-unique-zip-proof',
     replacementRequested=False,currentPathAbsenceProven=False,uniqueFinalZipProven=False,
     promotion=dict(observed=True,accepted=True,win32Error=0),
     postPromotionValidation=dict(entered=True,returned=True,readableAndNonempty=False))
  check=dict(sizeBytes=1234,filesystemErrorValue=None,filesystemErrorCategory=None)
  attempt=dict(attempt=2,attemptBodyReturned=True,completedStepElapsedMs={},totalElapsedMs=0,promotionEvidence=promotion,
     outputCheckEvidence=dict(schema=1,scope='original-archive-output-checks;not-zip-integrity-or-current-path-proof',
       temporary=dict(check,state='stream-opened-nonempty'),final=dict(check,state='stream-open-failed',openWin32Error=32)))
  row['archiveAttemptTiming']=dict(schemaVersion=1,scope='current-archive-call-completed-step-wall-ms',clock='GetTickCount64',
     maxAttempts=2,maxPhases=8,attemptTotalIncludesRetryDelay=True,includesArchiveSetupOutsideAttempts=False,
     originalResultOrDurabilityProven=False,attemptsObserved=2,
     attempts=[dict(attempt=1,attemptBodyReturned=True,completedStepElapsedMs={},totalElapsedMs=0),attempt])
  def actual(value):return self.view([value],current)['precedingOperationCarry']['finalOutputAfterMove']['retainedRowComparison']
  self.assertEqual(actual(row),'reported-fields-agree')
  attempt['outputCheckEvidence']['final']['openWin32Error']=5
  self.assertEqual(actual(row),'reported-fields-conflict')
  row['archiveAttemptTiming']['attempts'][1]=False
  self.assertEqual(actual(row),'raw-final-check-unusable')
 def test_prior_archive_steps_keep_attempts_and_do_not_double_count(self):
  from log_lane_audit_selftest import ArchiveAttemptTimingTests
  current,row=self.carrying_availability()
  carry=current['priorOrdinaryExportTerminal']
  first=dict(attempt=1,totalElapsedMs=3000,completedStepElapsedMs={'sourceSnapshot':0,'tarProcess':1000,'retryDelay':50})
  second=dict(attempt=2,totalElapsedMs=1203,completedStepElapsedMs={'sourceSnapshot':80,'attemptCleanup':0})
  carry['archiveAttemptSteps']=dict(schema=1,attempts=[first,second])
  timing=row['archiveAttemptTiming']=ArchiveAttemptTimingTests.receipt()
  timing.update(attemptsObserved=2,attempts=[dict(copy.deepcopy(x),attemptBodyReturned=True) for x in (first,second)])
  def steps(rows):return self.view(rows,current)['precedingOperationCarry']['archiveAttemptSteps']
  self.assertEqual(steps([])['retainedRowComparison'],'terminal-not-observed')
  self.assertEqual(steps([row])['retainedRowComparison'],'reported-fields-agree')
  wall=steps([row])['wallBreakdown']
  raw=self.view([row],current)['precedingOperation']['terminal']['archiveTiming']['wallBreakdown']
  self.assertEqual(wall,raw)
  self.assertEqual((wall['archiveCallMs'],wall['attemptTotalsMs'],wall['attemptCount']),(4203,4203,2))
  self.assertEqual(wall['outsideAttemptTotalsMs'],0)
  self.assertEqual(wall['insideAttemptsNotAssignedToReportedStepsMs'],3073)
  self.assertEqual(wall['steps']['sourceSnapshot'],{'elapsedMs':80,'attemptsReportingStep':2})
  self.assertEqual(wall['steps']['attemptCleanup'],{'elapsedMs':0,'attemptsReportingStep':1})
  self.assertEqual(wall['steps']['retryDelay'],{'elapsedMs':50,'attemptsReportingStep':1})
  self.assertNotIn('archiveValidationAndPromotion',wall['steps'])
  self.assertEqual(steps([row,row])['retainedRowComparison'],'ambiguous-retained-terminals')
  self.assertEqual(steps([dict(row,archiveElapsedMs=4204)])['retainedRowComparison'],'reported-fields-conflict')
  without=dict(row);del without['archiveAttemptTiming']
  self.assertEqual(steps([without])['retainedRowComparison'],'raw-steps-not-observed')
  timing['attempts'][1]['completedStepElapsedMs']['sourceSnapshot']=81
  self.assertEqual(steps([row])['retainedRowComparison'],'reported-fields-conflict')
  timing['attempts'][1]['attemptBodyReturned']=False
  self.assertEqual(steps([row])['retainedRowComparison'],'raw-steps-unusable')
  result=self.view([],current)
  self.assertIsNone(result['terminal'])
  self.assertEqual(result['precedingOperationCarry']['terminal']['reported']['archiveElapsedMs'],4203)
  self.assertEqual(steps([])['attempts'],[first,second])

 def test_archive_wall_breakdown_requires_matching_completed_attempts(self):
  from log_lane_audit_selftest import ArchiveAttemptTimingTests
  report=ArchiveAttemptTimingTests.receipt()
  row=dict(self.terminal(),attempts=1,archiveElapsedMs=22,archiveAttemptTiming=report)
  def read(projection):return self.view([projection])['terminal']['archiveTiming']
  valid=read(row)['wallBreakdown']
  self.assertEqual(valid['outsideAttemptTotalsMs'],3)
  self.assertEqual(valid['insideAttemptsNotAssignedToReportedStepsMs'],0)
  for change in ({'attempts':2},{'attempts':True},{'archiveElapsedMs':18},{'archiveElapsedMs':False}):
   self.assertNotIn('wallBreakdown',read(dict(row,**change)))
  self.assertNotIn('wallBreakdown',a.archive_attempt_timing_record({'archiveAttemptTiming':report}))
  attempt=report['attempts'][0]
  attempt['completedStepElapsedMs']['unrecognized']=0
  self.assertNotIn('wallBreakdown',read(row))
  del attempt['completedStepElapsedMs']['unrecognized']
  attempt.update(attemptBodyReturned=False,totalElapsedMs=None)
  self.assertNotIn('wallBreakdown',read(row))

 def carrying_child_failures(self):
  from log_lane_audit_selftest import ArchiveAttemptTimingTests
  current,row=self.carrying_availability()
  carry=current['priorOrdinaryExportTerminal']; carry['terminal'].update(ok=False,bodyResultOk=False)
  row['ok']=False
  phases={'commandPreparation':0,'tarProcess':2}
  steps=[dict(attempt=i,totalElapsedMs=3,completedStepElapsedMs=dict(phases)) for i in (1,2)]
  failure=dict(schema=1,backend='tar',completion='child-exit',ran=True,captureFinalizationEntered=True,
               wrapperExitCode=17,win32Error=0,captureChannel='stderr',retainedOutputBytes=22)
  failures=[dict(attempt=1,zipCompressionRequested='level-5',archiveChildFailure=failure),
            dict(attempt=2,zipCompressionRequested='tar-default',archiveChildFailure=dict(failure,retainedOutputBytes=0))]
  carry['archiveAttemptSteps']=dict(schema=1,attempts=steps,childFailures=failures)
  row['archiveAttemptTiming']=dict(ArchiveAttemptTimingTests.receipt(),attemptsObserved=2,
      attempts=[dict(copy.deepcopy(step),attemptBodyReturned=True,**copy.deepcopy({k:v for k,v in fail.items() if k!='attempt'}))
                for step,fail in zip(steps,failures)])
  return current,row,failures

 def test_prior_child_failures_are_distinct_from_current_outcome_and_from_timing(self):
  current,row,failures=self.carrying_child_failures()
  def read(rows):return self.view(rows,current)['precedingOperationCarry']
  field='archiveChildFailures'
  self.assertEqual(read([])[field],dict(state='reported-archive-child-failures',failures=failures,retainedRowComparison='terminal-not-observed'))
  self.assertIsNone(self.view([],current)['terminal'])
  self.assertEqual(read([row])[field]['retainedRowComparison'],'reported-fields-agree')
  self.assertEqual(read([row,row])[field]['retainedRowComparison'],'ambiguous-retained-terminals')
  raw=row['archiveAttemptTiming']['attempts'][1]
  original=raw.pop('archiveChildFailure')
  self.assertEqual(read([row])[field]['retainedRowComparison'],'raw-child-failure-not-observed')
  raw['archiveChildFailure']=dict(original,retainedOutputBytes=1)
  self.assertEqual(read([row])[field]['retainedRowComparison'],'reported-fields-conflict')
  raw['archiveChildFailure']['retainedOutputBytes']=False
  self.assertEqual(read([row])[field]['retainedRowComparison'],'raw-child-failure-unusable')
  self.assertEqual(read([row])['archiveAttemptSteps']['retainedRowComparison'],'reported-fields-agree')
  steps=current['priorOrdinaryExportTerminal']['archiveAttemptSteps']
  for invalid in (None,[],failures*2,failures[::-1],[dict(failures[0],unexpected=True)],
                  [dict(failures[1],zipCompressionRequested='dotnet-optimal')]):
   steps['childFailures']=invalid
   self.assertEqual(read([])[field]['state'],'invalid-fields')
   self.assertEqual(read([])['archiveAttemptSteps']['state'],'reported-completed-steps')
  steps['childFailures']=failures
  current['priorOrdinaryExportTerminal']['terminal'].update(ok=True,bodyResultOk=True)
  self.assertEqual(read([])[field]['state'],'invalid-fields')
  steps['childFailures']=failures[:1]
  self.assertEqual(read([])[field]['state'],'reported-archive-child-failures')

 def carrying_temporary_lookups(self):
  from archive_cleanup_return_reader_selftest import ArchiveCleanupReturnTests
  current,row,_=self.carrying_child_failures()
  fixture=ArchiveCleanupReturnTests()
  packed=current['priorOrdinaryExportTerminal']['archiveAttemptSteps']
  lookups=[]
  for step,raw in zip(packed['attempts'],row['archiveAttemptTiming']['attempts']):
   for item in (step,raw):
    item['completedStepElapsedMs']['attemptCleanup']=1
   attempt=fixture.cleanup_candidate_attempt()
   raw['cleanupEvidence']=attempt['cleanupEvidence']
   evidence=copy.deepcopy(attempt['failedTemporaryAfterCleanup'])
   evidence.update(elapsedMs=1,bytes=10240 if step['attempt']==1 else 0)
   raw['failedTemporaryAfterCleanup']=copy.deepcopy(evidence)
   errors=[{key:site[key] for key in ('errorValue','errorCategory')} for site in raw['cleanupEvidence']['sites'][-2:]]
   lookups.append(dict(attempt=step['attempt'],failedCleanupErrors=errors,failedTemporaryAfterCleanup=evidence))
  packed['failedTemporaryLookups']=lookups
  return current,row,lookups

 def test_prior_temporary_lookup_agrees_with_original_and_invalid_metadata_is_optional(self):
  current,row,lookups=self.carrying_temporary_lookups()
  field='archiveTemporaryLookups'
  success='reported-archive-temporary-lookups'
  def read(rows):return self.view(rows,current)['precedingOperationCarry']
  self.assertEqual(read([])[field],dict(state=success,lookups=lookups,retainedRowComparison='terminal-not-observed'))
  self.assertIsNone(self.view([],current)['terminal'])
  self.assertEqual(read([row])[field]['retainedRowComparison'],'reported-fields-agree')
  broken=copy.deepcopy(row)
  tail=broken['archiveAttemptTiming']['attempts'][1]
  tail['cleanupEvidence']['sites'][-1]['errorValue']=5
  self.assertEqual(read([broken])[field]['retainedRowComparison'],'reported-fields-conflict')
  tail.pop('failedTemporaryAfterCleanup')
  self.assertEqual(read([broken])[field]['retainedRowComparison'],'raw-temporary-lookup-not-observed')
  packed=current['priorOrdinaryExportTerminal']['archiveAttemptSteps']
  invalid=[dict(lookups[0],failedTemporaryAfterCleanup=dict(lookups[0]['failedTemporaryAfterCleanup'],elapsedMs=2)),
           dict(lookups[0],failedCleanupErrors=[dict(errorValue=0,errorCategory='system')]*2),
           dict(lookups[0],failedTemporaryAfterCleanup=dict(lookups[0]['failedTemporaryAfterCleanup'],bytes=True))]
  for wrong in (lookups[::-1],[lookups[0]]*2,*([item] for item in invalid)):
   packed['failedTemporaryLookups']=wrong
   readback=read([])
   self.assertEqual(readback[field]['state'],'invalid-fields')
   self.assertEqual(readback['archiveAttemptSteps']['state'],'reported-completed-steps')
   self.assertEqual(readback['archiveChildFailures']['state'],'reported-archive-child-failures')
  packed['failedTemporaryLookups']=lookups
  current['priorOrdinaryExportTerminal']['terminal']['cleanupOk']=True
  self.assertEqual(read([])[field]['state'],'invalid-fields')
  current['priorOrdinaryExportTerminal']['terminal'].update(cleanupOk=False,ok=True,bodyResultOk=True)
  self.assertEqual(read([])[field]['state'],'invalid-fields')
  packed['failedTemporaryLookups']=lookups[:1]
  self.assertEqual(read([])[field]['state'],success)

 def test_cross_archive_temporary_lookup_complements_attempts_without_suppressing_core(self):
  current,_,lookups=self.carrying_temporary_lookups()
  def carrying(rows):
   view=copy.deepcopy(current)
   view['priorOrdinaryExportTerminal']['archiveAttemptSteps']['failedTemporaryLookups']=rows
   return view
  def view(items):
   cross,_=self.cross_view([(self.availability(),[])]+[(item,[]) for item in items])
   return cross['receipts'][0]['precedingCarryFromOtherArchive']
  first,second=carrying(lookups[:1]),carrying(lookups[1:])
  field='archiveTemporaryLookups'
  self.assertEqual(view([second,first])['details'][field]['value'],lookups)
  conflict=dict(lookups[1],failedTemporaryAfterCleanup=dict(lookups[1]['failedTemporaryAfterCleanup'],bytes=1))
  compared=view([first,second,carrying([conflict])])
  self.assertTrue(compared['details'][field]['conflicting'])
  self.assertIsNone(compared['details'][field]['value'])
  self.assertIsNotNone(compared['core'])

 def test_bad_prior_archive_steps_do_not_hide_terminal(self):
  current,_=self.carrying_availability()
  carry=current['priorOrdinaryExportTerminal']
  first=dict(attempt=1,totalElapsedMs=4000,completedStepElapsedMs={'attemptCleanup':0})
  second=dict(attempt=2,totalElapsedMs=200,completedStepElapsedMs={'sourceSnapshot':100})
  for invalid in (None,dict(schema=True,attempts=[first,second]),dict(schema=1,attempts=[first]),
     *(dict(schema=1,attempts=[first,dict(second,**change)]) for change in
       (dict(attempt=True),dict(totalElapsedMs=204),dict(completedStepElapsedMs={'retryDelay':0}),
        dict(completedStepElapsedMs={'sourceSnapshot':True}),dict(completedStepElapsedMs={'unexpected':0}),
        dict(completedStepElapsedMs={'sourceSnapshot':150,'attemptCleanup':51})))):
   with self.subTest(invalid=invalid):
    carry['archiveAttemptSteps']=invalid
    result=self.view([],current)['precedingOperationCarry']
    self.assertEqual(result['state'],'reported-in-memory-preceding-terminal')
    self.assertEqual(result['archiveAttemptSteps']['state'],'invalid-fields')
    self.assertFalse(result['archiveAttemptSteps']['attempts'])

 def test_duplicate_availability_refused_in_full_archive(self):
  source=(Path(__file__).resolve().parents[1]/'native/main.cpp').read_text('utf-8-sig')
  carrying,_=self.carrying_availability()
  with tempfile.TemporaryDirectory(prefix='ymcc-export-duplicate-') as d:
   p=Path(d)/'example.zip'
   with warnings.catch_warnings():
    warnings.simplefilter('ignore',UserWarning)
    with zipfile.ZipFile(p,'w') as z:
     z.writestr('log-export-availability.json',json.dumps(carrying));z.writestr('log-export-availability.json',json.dumps(carrying));z.writestr('native-lifecycle.log',json.dumps(self.terminal())+'\n')
   r=a.audit_zip(p,a.source_policy(source))
  self.assertEqual(r['commonEvidence']['ordinaryExportTransaction']['state'],'receipt-unavailable')
  self.assertIsNone(r['commonEvidence']['ordinaryExportTransaction']['precedingOperationCarry'])
 def cross_view(self, batches, *, capacity=None, manifests=None, basenames=None, snapshots=None):
  collector=a.CrossArchiveExportTerminals()
  if capacity is not None:collector.MAX_OPERATIONS=capacity
  with tempfile.TemporaryDirectory(prefix='ymcc-export-cross-archive-') as d:
   audits=[]
   for i,(availability,rows) in enumerate(batches):
    p=Path(d)/(basenames[i] if basenames else f'{i}.zip')
    with zipfile.ZipFile(p,'w') as z:
     for text in (manifests or {}).get(i,()):
      with warnings.catch_warnings():
       warnings.simplefilter('ignore',UserWarning)
       z.writestr(a.AUDIT_MANIFEST_NAME,text)
     if availability is not None:z.writestr('log-export-availability.json',json.dumps(availability))
     if i in (snapshots or {}):z.writestr('log-export-snapshot.json',json.dumps(snapshots[i]))
     if rows:z.writestr('native-lifecycle.log.1',''.join(json.dumps(row)+'\n' for row in rows))
     z.writestr('fixture-id.txt',str(i))
    audits.append(a.audit_zip(p,{'baseEvents':[]},cross_archive_exports=collector))
   # Giving the same archive again does not create a second observation.
   audits.append(a.audit_zip(Path(d)/(basenames[0] if basenames else '0.zip'),{'baseEvents':[]},cross_archive_exports=collector))
   return collector.evidence(audits),audits
 def test_legacy_public_attempt_pair_preserves_one_operation_and_distinct_artifacts(self):
  receipt=self.availability();name='YeManCC-logs-20261118-003719-7012-10256843-1.zip'
  temp=name+'.attempt-7012-10266843-0-1.zip'
  cross,audits=self.cross_view([(receipt,[]),(receipt,[]),(None,[self.terminal()])],basenames=[name,temp,'later.zip'])
  pair,=cross['legacyPublicAttemptPairs']['pairs']
  self.assertEqual(pair['state'],'distinct-archive-bytes')
  self.assertEqual(pair['operation'],receipt['exportOperation'])
  self.assertEqual(len(pair['finalNameReferences']),1)
  self.assertEqual(pair['legacyAttemptNameReferences'][0]['filenameAttempt'],1)
  self.assertIs(cross['legacyPublicAttemptPairs']['desktopFileCountProven'],False)
  self.assertTrue(all(x['state']=='terminal-in-another-archive' for x in cross['receipts']))
  alias=dict(audits[0],zipPath=audits[1]['zipPath'])
  same,=a.legacy_public_archive_pairs([audits[0],alias])['pairs']
  self.assertEqual(same['state'],'same-archive-bytes-under-both-names')
 def test_legacy_filename_must_match_receipt_identity_and_identical_availability_bytes(self):
  receipt=self.availability();name='YeManCC-logs-20261118-003719-7012-10256843-1.zip'
  temp=name+'.attempt-7012-10266843-0-1.zip'
  changed=copy.deepcopy(receipt);changed['unrelatedMetadata']=True
  _,audits=self.cross_view([(receipt,[]),(receipt,[]),(changed,[])],basenames=[name,temp,'other.zip'])
  left,right,other,_=audits
  self.assertFalse(a.legacy_public_archive_pairs([left,dict(other,zipPath=temp)])['pairs'])
  for suffix in ('7013-10266843-0-1.zip','7012-10256842-0-1.zip','7012-10266843-00-1.zip',
                 '7012-10266843-0-3.zip','7012-10266843-0-1-rename.zip','7012-18446744073709551616-0-1.zip'):
   with self.subTest(suffix=suffix):
    bad=dict(right,zipPath=name+'.attempt-'+suffix)
    self.assertFalse(a.legacy_public_archive_pairs([left,bad])['pairs'])
  self.assertFalse(a.legacy_public_archive_pairs([left,right],max_groups=0)['pairs'])
  self.assertEqual(a.legacy_public_archive_pairs([left,right],max_groups=0)['inputArchivesOmittedByGroupLimit'],2)
 def quick_availability(self):
  v=self.availability()
  v['windowsSleepDiagnostics']=dict(schema='ymcc.sleep-export-diagnostics.v1',status='NOT_COLLECTED',reason='skipped-by-request',reportAvailable=False,queryAttempts=0)
  return v
 def test_quick_mode_and_skip_are_separate_from_terminal(self):
  v=self.quick_availability();row=dict(self.terminal(),exportMode='quick')
  cross,audits=self.cross_view([(v,[]),(None,[row])],manifests={0:['logExportMode=quick\n'],1:['logExportMode=full\n']})
  local=audits[0]['commonEvidence']['ordinaryExportTransaction']['modeEvidence']
  self.assertEqual(local['manifest']['reportedMode'],'quick')
  self.assertEqual(local['windowsQuery']['disposition'],'skipped-by-request')
  self.assertIsNone(local['terminal']['reportedMode'])
  joined=cross['receipts'][0]['modeEvidence']
  self.assertEqual(joined['terminal']['association'],'terminal-in-another-archive')
  self.assertEqual(joined['terminal']['reportedMode'],'quick')
  self.assertEqual(joined['manifest']['reportedMode'],'quick')
  self.assertEqual(joined['conflicts'],[])
 def cooldown_availability(self):
  current=self.availability()
  current['exportOperation'].update(id='7012-10296843-3',ordinal='3',startedTickMs='10296843')
  windows=current['windowsSleepDiagnostics']; previous=windows.pop('query')
  windows.update(reason='recent-query-deadline',queryAttempts=0,querySuppressed=True,recentDeadline={
   'originalExportId':self.operation()['id'],'completedTickMs':'10266863','powerGeneration':'10256000',
   'ageMs':29980,'retryAfterMs':30020,'cooldownMs':60000,'query':previous})
  current['exportPhaseTiming']['elapsedMs']['windowsSystemPowerReport']=0
  return current
 def test_cooldown_origin_joins_named_nonadjacent_terminal_in_archive(self):
  current=self.cooldown_availability(); original=self.terminal(); intermediate=copy.deepcopy(original)
  intermediate['exportOperation'].update(id='7012-10286843-2',ordinal='2',startedTickMs='10286843')
  intermediate['phaseElapsedMs']['windowsSystemPowerReport']=0
  with tempfile.TemporaryDirectory(prefix='ymcc-export-cooldown-origin-') as d:
   p=Path(d)/'example.zip'
   with zipfile.ZipFile(p,'w') as z:
    z.writestr('log-export-availability.json',json.dumps(current))
    z.writestr('native-lifecycle.log.1',json.dumps(original)+'\n')
    z.writestr('native-lifecycle.log',json.dumps(intermediate)+'\n')
   view=a.audit_zip(p,{'baseEvents':[]})['commonEvidence']['ordinaryExportTransaction']
  origin=view['cooldownOrigin']; self.assertEqual(origin['state'],'unique-reported-origin-terminal')
  self.assertEqual(origin['candidateRows'],1);self.assertEqual(origin['terminal']['file'],'native-lifecycle.log.1')
  self.assertEqual(origin['terminal']['line'],1);self.assertEqual(origin['terminal']['operation'],original['exportOperation'])
  self.assertEqual(origin['terminal']['terminal']['phaseTiming']['elapsedMs']['windowsSystemPowerReport'],10015)
  self.assertEqual(view['precedingOperation']['terminal']['operation'],intermediate['exportOperation'])
  self.assertEqual(view['state'],'terminal-not-observed');self.assertEqual(view['windowsQuery']['query'],{})
  self.assertFalse(origin['currentQueryExecutedInferred']);self.assertFalse(origin['currentExportOutcomeInferred'])
 def test_cooldown_origin_distinguishes_missing_conflicting_and_duplicate_terminals(self):
  original=self.terminal(); foreign=dict(original,writerProcessCreation100ns='134394066260992256')
  wrong_operation=copy.deepcopy(original);wrong_operation['exportOperation']['ordinal']='2'
  for rows,state,count in (([],'origin-terminal-not-observed',0),
      ([foreign],'origin-terminal-writer-or-operation-conflict',1),
      ([wrong_operation],'origin-terminal-writer-or-operation-conflict',1),
      ([original,foreign],'ambiguous-origin-terminals',2),([original,original],'ambiguous-origin-terminals',2)):
   with self.subTest(state=state,rows=len(rows)):
    origin=self.view(rows,self.cooldown_availability())['cooldownOrigin']
    self.assertEqual(origin['state'],state);self.assertEqual(origin['candidateRows'],count)
    self.assertIsNone(origin['terminal'])
 def test_cooldown_origin_refuses_bad_references_and_uncorroborated_suppression(self):
  for additions,state in (({'originalExportId':'7013-10256843-1'},'origin-reference-conflict'),
      ({'originalExportId':'7012-10296843-3'},'origin-reference-conflict'),
      ({'originalExportId':'7012-010256843-1'},'origin-reference-conflict'),
      ({'completedTickMs':'10256842'},'origin-reference-conflict'),
      ({'completedTickMs':'10296844'},'origin-reference-conflict'),
      ({'originalExportId':''},'origin-reference-incomplete'),({'retryAfterMs':30021},'cooldown-receipt-invalid')):
   with self.subTest(additions=additions):
    receipt=self.cooldown_availability();receipt['windowsSleepDiagnostics']['recentDeadline'].update(additions)
    origin=self.view([self.terminal()],receipt)['cooldownOrigin']
    self.assertEqual(origin['state'],state);self.assertIsNone(origin['terminal'])
  self.assertEqual(self.view()['cooldownOrigin']['state'],'no-reported-cooldown')
 def test_full_deadline_and_cooldown_are_distinct(self):
  from log_lane_audit_selftest import ExportPhaseAndDeadlineTests
  current=self.availability();cooling=dict(current,**ExportPhaseAndDeadlineTests.reused())
  for receipt,disposition in ((current,'current-query-attempt-reported'),(cooling,'suppressed-recent-deadline')):
   with self.subTest(disposition=disposition):
    _,audits=self.cross_view([(receipt,[dict(self.terminal(),exportMode='full')])],manifests={0:['logExportMode=full\n']})
    local=audits[0]['commonEvidence']['ordinaryExportTransaction']['modeEvidence']
    self.assertEqual(local['manifest']['reportedMode'],'full')
    self.assertEqual(local['terminal']['reportedMode'],'full')
    self.assertEqual(local['windowsQuery']['disposition'],disposition)
    self.assertEqual(local['conflicts'],[])
 def test_legacy_mode_missing_never_inferred_from_query(self):
  for receipt in (self.availability(),self.quick_availability()):
   cross,audits=self.cross_view([(receipt,[self.terminal()])],manifests={0:['processId=7012\n']})
   local=audits[0]['commonEvidence']['ordinaryExportTransaction']['modeEvidence']
   self.assertEqual(local['manifest']['state'],'missing-field')
   self.assertEqual(local['terminal']['state'],'missing-field')
   self.assertIsNone(cross['receipts'][0]['modeEvidence']['terminal']['reportedMode'])
 def test_duplicate_manifest_mode_and_duplicate_member_refused(self):
  for texts,state in ((['logExportMode=full\nlogExportMode=full\n'],'ambiguous-field'),
                      (['logExportMode=full\n','logExportMode=quick\n'],'manifest-unavailable')):
   with self.subTest(state=state):
    _,audits=self.cross_view([(self.quick_availability(),[])],manifests={0:texts})
    local=audits[0]['commonEvidence']['ordinaryExportTransaction']['modeEvidence']
    self.assertIsNone(audits[0]['manifest']['logExportMode'])
    self.assertEqual(local['manifest']['state'],state)
    self.assertEqual(local['conflicts'],[])
 def test_unrelated_manifest_ambiguity_does_not_hide_mode(self):
  _,audits=self.cross_view([(self.quick_availability(),[])],manifests={0:['appVersion=1\nappVersion=2\nlogExportMode=quick\n']})
  local=audits[0]['commonEvidence']['ordinaryExportTransaction']['modeEvidence']
  self.assertEqual(local['manifest']['selectionStatus'],'selected-with-ambiguous-fields')
  self.assertEqual(local['manifest']['reportedMode'],'quick')
 def test_unsupported_mode_and_malformed_query_do_not_supply_conflict(self):
  v=self.quick_availability();v['windowsSleepDiagnostics']['queryAttempts']=False
  _,audits=self.cross_view([(v,[dict(self.terminal(),exportMode=True)])],manifests={0:['logExportMode=future\n']})
  local=audits[0]['commonEvidence']['ordinaryExportTransaction']['modeEvidence']
  self.assertEqual(local['manifest']['state'],'unsupported-value')
  self.assertEqual(local['terminal']['state'],'invalid-field')
  self.assertEqual(local['windowsQuery']['disposition'],'not-established')
  self.assertEqual(local['conflicts'],[])
 def test_mode_conflicts_keep_reported_sources_and_respect_exact_operation(self):
  receipt=self.quick_availability();row=dict(self.terminal(),exportMode='full')
  cross,_=self.cross_view([(receipt,[]),(None,[row])],manifests={0:['logExportMode=quick\n']})
  mode=cross['receipts'][0]['modeEvidence']
  self.assertEqual(mode['conflicts'],['manifest-vs-terminal','terminal-vs-windows-query'])
  self.assertEqual(mode['terminal']['reportedMode'],'full')
  cross,audits=self.cross_view([(receipt,[dict(row,pid=9)])],manifests={0:['logExportMode=full\n']})
  local=audits[0]['commonEvidence']['ordinaryExportTransaction']['modeEvidence']
  self.assertEqual(local['conflicts'],['manifest-vs-windows-query'])
  self.assertEqual(local['terminal']['association'],'matching-operation-writer-conflict')
  self.assertIsNone(cross['receipts'][0]['modeEvidence']['terminal']['reportedMode'])
  receipt.pop('exportOperation')
  _,audits=self.cross_view([(receipt,[row])],manifests={0:['logExportMode=full\n']})
  local=audits[0]['commonEvidence']['ordinaryExportTransaction']['modeEvidence']
  self.assertEqual(local['windowsQuery']['disposition'],'skipped-by-request')
  self.assertFalse(local['windowsQuery']['supportedReceiptOperation'])
  self.assertEqual(local['conflicts'],[])
 def test_differing_exact_terminals_do_not_supply_a_mode(self):
  full=dict(self.terminal(),exportMode='full');quick=dict(full,exportMode='quick')
  cross,_=self.cross_view([(self.availability(),[]),(None,[full]),(None,[quick])],manifests={0:['logExportMode=full\n']})
  mode=cross['receipts'][0]['modeEvidence']
  self.assertEqual(mode['terminal']['association'],'ambiguous-or-conflicting-terminal')
  self.assertIsNone(mode['terminal']['reportedMode'])
 def test_cross_archive_finds_exact_terminal_without_changing_local_state(self):
  full=self.availability();quick=copy.deepcopy(full)
  quick['exportOperation'].update(writerPid=7013,id='7013-10256843-1')
  quick['windowsSleepDiagnostics']=dict(schema='ymcc.sleep-export-diagnostics.v1',status='NOT_COLLECTED',reason='skipped-by-request',reportAvailable=False,queryAttempts=0)
  row=self.terminal();row['exportMode']='full'
  cross,audits=self.cross_view([(full,[]),(quick,[row])])
  self.assertEqual(cross['uniqueArchives'],2);first,second=cross['receipts']
  self.assertEqual(first['state'],'terminal-in-another-archive');self.assertEqual(first['terminalRows'],1)
  self.assertEqual(first['terminal']['reported']['exportElapsedMs'],14600)
  self.assertEqual(first['terminal']['reported']['exportMode'],'full')
  self.assertEqual(first['witnesses'][0]['archiveSha256'],audits[1]['zipSha256'])
  self.assertEqual(second['state'],'terminal-not-observed');self.assertIsNone(second['terminal'])
  self.assertEqual(audits[0]['commonEvidence']['ordinaryExportTransaction']['state'],'terminal-not-observed')
  self.assertFalse(cross['desktopFinalZipCountOrIpcDeliveryProven'])
 def test_cross_archive_reused_pid_does_not_pair(self):
  row=self.terminal();row['writerProcessCreation100ns']='134394066260992256';row['exportOperation']=self.operation(birth=row['writerProcessCreation100ns'])
  cross,_=self.cross_view([(self.availability(),[]),(None,[row])]);self.assertEqual(cross['receipts'][0]['state'],'terminal-not-observed')
 def test_cross_archive_carry_has_exact_identity_without_becoming_native_terminal(self):
  current,row=self.carrying_availability()
  other=self.availability()
  other['exportOperation']['writerProcessCreation100ns']='134394066260992256'
  cross,audits=self.cross_view([(current,[]),(self.availability(),[]),(other,[])])
  carrier,target,reused=cross['receipts']
  self.assertNotIn('precedingCarryFromOtherArchive',carrier)
  self.assertNotIn('precedingCarryFromOtherArchive',reused)
  self.assertEqual(target['state'],'terminal-not-observed');self.assertEqual(target['terminalRows'],0)
  self.assertIsNone(target['terminal'])
  carried=target['precedingCarryFromOtherArchive']
  self.assertEqual(carried['carrierArchives'],1);self.assertFalse(carried['originalLogRow'])
  self.assertEqual(carried['nativeTerminalProjectionComparison'],'terminal-not-observed')
  self.assertEqual(carried['core']['terminal']['reported']['exportElapsedMs'],14600)
  self.assertEqual(carried['witnesses'][0]['archiveSha256'],audits[0]['zipSha256'])
  self.assertEqual(carried['witnesses'][0]['operation'],current['exportOperation'])
  cross,_=self.cross_view([(current,[]),(self.availability(),[]),(None,[row])])
  target=cross['receipts'][1]
  self.assertEqual(target['terminalRows'],1)
  self.assertEqual(target['precedingCarryFromOtherArchive']['nativeTerminalProjectionComparison'],'reported-core-fields-agree')
  row['exportElapsedMs']+=1
  cross,_=self.cross_view([(current,[]),(self.availability(),[]),(None,[row])])
  target=cross['receipts'][1]
  self.assertEqual(target['terminal']['reported']['exportElapsedMs'],14601)
  self.assertEqual(target['precedingCarryFromOtherArchive']['nativeTerminalProjectionComparison'],'reported-core-fields-conflict')
 def test_cross_archive_carry_conflicts_are_separate_from_optional_missing_fields(self):
  current,_=self.carrying_availability()
  evidence=dict(schema=1,attempt=2,state='stream-opened-nonempty',sizeBytes=1234)
  with_detail=copy.deepcopy(current); with_detail['priorOrdinaryExportTerminal']['finalOutputAfterMove']=evidence
  conflict=copy.deepcopy(with_detail); conflict['priorOrdinaryExportTerminal']['finalOutputAfterMove']['sizeBytes']=5678
  mixed=[(self.availability(),[]),(current,[]),(with_detail,[])]
  def projected(inputs):return self.cross_view(inputs)[0]['receipts'][0]['precedingCarryFromOtherArchive']
  v=projected(mixed)
  self.assertEqual(v['carrierArchives'],2)
  self.assertEqual(v['details']['finalOutputAfterMove']['value']['sizeBytes'],1234)
  v=projected(mixed+[(conflict,[])])
  self.assertEqual(v['state'],'reported-preceding-carry');self.assertIsNotNone(v['core'])
  self.assertTrue(v['details']['finalOutputAfterMove']['conflicting'])
  self.assertIsNone(v['details']['finalOutputAfterMove']['value'])
  conflict['priorOrdinaryExportTerminal']['terminal']['exportElapsedMs']+=1
  v=projected(mixed+[(conflict,[])])
  self.assertEqual(v['state'],'conflicting-carried-terminals');self.assertIsNone(v['core'])
 def test_compare_supplied_zip_size_with_exact_reported_final_output(self):
  from log_lane_audit_selftest import ArchiveAttemptTimingTests
  current,row=self.carrying_availability()
  available=self.availability()
  _,archives=self.cross_view([(available,[])])
  length=archives[0]['zipSizeBytes']
  self.assertGreater(length,0)
  timing=ArchiveAttemptTimingTests.receipt()
  timing.update(attemptsObserved=2,attempts=[timing['attempts'][0],dict(copy.deepcopy(timing['attempts'][0]),attempt=2)])
  def returned(size):
   result=copy.deepcopy(timing)
   attempt=result['attempts'][1]
   opened=self.output_state('stream-opened-nonempty',size)
   attempt['outputCheckEvidence']=dict(schema=1,scope='original-archive-output-checks;not-zip-integrity-or-current-path-proof',temporary=opened,final=opened)
   attempt['promotionEvidence']=dict(schema=1,scope='original-MoveFileExW-and-post-promotion-validation;not-current-path-or-unique-zip-proof',replacementRequested=False,currentPathAbsenceProven=False,uniqueFinalZipProven=False,promotion=dict(observed=True,accepted=True,win32Error=0),postPromotionValidation=dict(entered=True,returned=True,readableAndNonempty=True))
   return dict(row,archiveAttemptTiming=result)
  def summary(*extras):
   cut,_=self.cross_view([(available,[]),*extras]);return cut['receipts'][0].get('outputSizeComparison')
  final=returned(length)
  same=summary((None,[final]))
  self.assertEqual((same['state'],same['suppliedZipBytes'],same['reportedFinalSizeBytes'],same['sources']),('same-size',length,length,['native-terminal']))
  self.assertFalse(same['sameFileOrContentProven']);self.assertFalse(same['desktopZipCountProven'])
  self.assertEqual(summary((None,[returned(length+1)]))['state'],'different-size')
  self.assertIsNone(summary((None,[dict(final,attempts=1)])))
  self.assertIsNone(summary((None,[dict(final,pid=9)])))
  self.assertEqual(summary((None,[final]),(None,[returned(length+1)]))['state'],'conflicting-report')
  carry=current['priorOrdinaryExportTerminal']
  carry['finalOutputAfterMove']=dict(schema=1,attempt=2,state='stream-opened-nonempty',sizeBytes=length)
  self.assertEqual(summary((current,[]))['sources'],['preceding-terminal-carry'])
  self.assertEqual(summary((current,[]),(None,[final]))['sources'],['native-terminal','preceding-terminal-carry'])
  self.assertEqual(summary((current,[]),(None,[returned(length+1)]))['state'],'conflicting-report')
  self.assertEqual(summary((current,[]),(None,[final,final]))['state'],'conflicting-report')
 def test_cross_archive_child_failures_merge_by_attempt_and_report_overlapping_conflict(self):
  current,_,failures=self.carrying_child_failures()
  def carrying(rows):
   view=copy.deepcopy(current)
   view['priorOrdinaryExportTerminal']['archiveAttemptSteps']['childFailures']=rows
   return view
  def detail(items):
   cross,_=self.cross_view([(self.availability(),[])]+[(item,[]) for item in items])
   return cross['receipts'][0]['precedingCarryFromOtherArchive']['details']['archiveChildFailures']
  first,second=carrying(failures[:1]),carrying(failures[1:])
  self.assertEqual(detail([second,first])['value'],failures)
  conflict=dict(failures[1],archiveChildFailure=dict(failures[1]['archiveChildFailure'],retainedOutputBytes=1))
  compared=detail([first,second,carrying([conflict])])
  self.assertTrue(compared['conflicting']); self.assertIsNone(compared['value'])

 def test_copy_failure_cross_archive_carry_conflict_is_optional(self):
  current,_=self.carrying_availability(); original=copy.deepcopy(current)
  field='snapshotCopyFailureEvidence'; original['priorOrdinaryExportTerminal'][field]=self.copy_failure_wrapper(lost=1)
  invalid=copy.deepcopy(original); invalid['priorOrdinaryExportTerminal'][field]=False
  conflict=copy.deepcopy(original); conflict['priorOrdinaryExportTerminal'][field]=self.copy_failure_wrapper()
  inputs=[(self.availability(),[]),(original,[]),(invalid,[]),(current,[])]
  def detail(items):return self.cross_view(items)[0]['receipts'][0]['precedingCarryFromOtherArchive']
  view=detail(inputs); data=view['details'][field]
  self.assertEqual(view['carrierArchives'],3);self.assertEqual(data['unusableArchives'],1);self.assertFalse(data['conflicting'])
  self.assertEqual(data['value']['coverage'],a.PRIOR_COPY_FAILURE_STATES[1]);self.assertEqual(data['reportedArchives'],1)
  view=detail(inputs+[(conflict,[])]);data=view['details'][field]
  self.assertTrue(data['conflicting']);self.assertIsNone(data['value']);self.assertIsNotNone(view['core']);self.assertFalse(view['originalLogRow'])
 def test_cross_archive_carry_limit_and_invalid_carrier_do_not_change_native_coverage(self):
  current,_=self.carrying_availability()
  invalid=copy.deepcopy(current)
  invalid['priorOrdinaryExportTerminal']['exportOperation']['startedTickMs']='10458843'
  cross,_=self.cross_view([(self.availability(),[]),(invalid,[]),(current,[])],capacity=0)
  self.assertEqual(cross['precedingCarryCoverage'],dict(operationsIndexed=0,carriersBeyondOperationLimit=1))
  self.assertNotIn('precedingCarryFromOtherArchive',cross['receipts'][0])
  self.assertEqual(cross['receipts'][0]['terminalRows'],0)
 def test_cross_archive_repeated_projection_is_distinct_from_duplicate_within_archive(self):
  row=self.terminal()
  cut,_=self.cross_view([(self.availability(),[]),(None,[row]),(None,[row])]);v=cut['receipts'][0]
  self.assertEqual(v['state'],'consistent-projection-in-multiple-archives');self.assertEqual(v['terminalArchives'],2)
  self.assertFalse(cut['matchingProjectionProvesSamePhysicalRow'])
  cut,_=self.cross_view([(self.availability(),[]),(None,[row,row])]);v=cut['receipts'][0]
  self.assertEqual(v['state'],'ambiguous-or-conflicting-terminal');self.assertTrue(v['repeatedWithinArchive']);self.assertIsNone(v['terminal'])
 def test_cross_archive_conflicts_cannot_select_a_success(self):
  row=self.terminal()
  for other,flag in ((dict(row,pid=9),'writerConflictRows'),(dict(row,ok=False),'differentTerminalProjections')):
   with self.subTest(flag=flag):
    cut,_=self.cross_view([(self.availability(),[]),(None,[row]),(None,[other])]);v=cut['receipts'][0]
    self.assertEqual(v['state'],'ambiguous-or-conflicting-terminal');self.assertTrue(v[flag]);self.assertIsNone(v['terminal'])
 def test_cross_archive_limit_does_not_turn_unindexed_rows_into_zero(self):
  row=self.terminal();other=dict(row,exportOperation=self.operation(ordinal='2'))
  cut,_=self.cross_view([(self.availability(),[]),(None,[other,row])],capacity=1);v=cut['receipts'][0]
  self.assertEqual(cut['rowsBeyondOperationLimit'],1);self.assertEqual(v['state'],'index-limit-or-terminal-not-observed');self.assertIsNone(v['terminalRows'])
 def test_cross_archive_timing_conflict_does_not_supply_one_versions_steps(self):
  from log_lane_audit_selftest import ArchiveAttemptTimingTests, ArchiveIndexWorkTests
  timing=ArchiveAttemptTimingTests.receipt();attempt=timing['attempts'][0]
  index=ArchiveIndexWorkTests.receipt();index.update(reusedRootInspections=1)
  index['digest'].update(enteredCalls=3,returnedCalls=3)
  attempt.update(indexWork=index,zipCompressionRequested='tar-default')
  row=dict(self.terminal(),attempts=1,archiveAttemptTiming=timing)
  batch=[(self.availability(),[]),(None,[row])]
  cut,_=self.cross_view(batch);v=cut['receipts'][0]
  self.assertEqual(v['archiveTiming']['attempts'][0]['indexWork']['reported']['reusedRootInspections'],1)
  self.assertEqual(v['archiveTiming']['attempts'][0]['completedStepElapsedMs']['tarProcess'],11)
  self.assertEqual(v['archiveTiming']['wallBreakdown']['outsideAttemptTotalsMs'],4184)
  other=copy.deepcopy(row);other['archiveAttemptTiming']['attempts'][0]['completedStepElapsedMs']['tarProcess']=10
  cut,_=self.cross_view(batch+[(None,[other])]);v=cut['receipts'][0]
  self.assertTrue(v['differentArchiveTimingProjections']);self.assertIsNone(v['archiveTiming'])
  self.assertTrue(v['terminal']['reported']['ok']);self.assertFalse(v['differentTerminalProjections'])
 def test_checkpoint_timing_compares_only_the_bound_attempt(self):
  from log_lane_audit_selftest import ArchiveAttemptTimingTests, OrdinaryExportSnapshotBoundaryTests
  available=self.availability(); current,row=self.carrying_availability()
  snapshot=OrdinaryExportSnapshotBoundaryTests.snapshot(2)
  snapshot['archiveAttemptContext']['exportOperation']=self.operation()
  prefix={'stagingPreparation':0,'sourceSnapshot':1516}
  attempts=[dict(attempt=1,totalElapsedMs=100,completedStepElapsedMs={'stagingPreparation':0,'sourceSnapshot':19}),
            dict(attempt=2,totalElapsedMs=2000,completedStepElapsedMs=dict(prefix,tarProcess=20))]
  row['archiveAttemptTiming']=dict(ArchiveAttemptTimingTests.receipt(),attemptsObserved=2,
                                 attempts=[dict(x,attemptBodyReturned=True) for x in copy.deepcopy(attempts)])
  current['priorOrdinaryExportTerminal']['archiveAttemptSteps']=dict(schema=1,attempts=copy.deepcopy(attempts))
  def compare(*others):
   cross,_=self.cross_view([(available,[]),*others],snapshots={0:snapshot})
   return cross['receipts'][0].get('snapshotTimingComparison')
  self.assertEqual(compare()['state'],'no-comparable-terminal')
  self.assertEqual(compare((None,[row]))['terminalElapsedMsBySource'],{'native-terminal':prefix})
  self.assertEqual(compare((current,[]))['terminalElapsedMsBySource'],{'preceding-terminal-carry':prefix})
  combined=compare((current,[]),(None,[row]))
  self.assertEqual((combined['state'],combined['attempt'],combined['checkpointElapsedMs']),('reported-timings-agree',2,prefix))
  self.assertEqual(combined['terminalElapsedMsBySource'],dict.fromkeys(('native-terminal','preceding-terminal-carry'),prefix))
  self.assertEqual(compare((None,[dict(row,attempts=1)]))['conflicts'],['native-terminal-attempt-count'])
  self.assertEqual(compare((None,[row,row]))['conflicts'],['native-terminal-evidence'])
  wrong_operation=dict(row,exportOperation=self.operation(birth='134394066260992256'),writerProcessCreation100ns='134394066260992256')
  self.assertEqual(compare((None,[wrong_operation]))['state'],'no-comparable-terminal')
  mismatch=copy.deepcopy(row); mismatch['archiveAttemptTiming']['attempts'][1]['completedStepElapsedMs']['sourceSnapshot']=1500
  self.assertEqual(compare((current,[]),(None,[mismatch]))['conflicts'],['native-terminal-vs-checkpoint'])
  missing=copy.deepcopy(row); del missing['archiveAttemptTiming']['attempts'][1]['completedStepElapsedMs']['sourceSnapshot']
  self.assertEqual(compare((None,[missing]))['state'],'no-comparable-terminal')
  conflict=copy.deepcopy(current); conflict['priorOrdinaryExportTerminal']['archiveAttemptSteps']['attempts'][1]['completedStepElapsedMs']['sourceSnapshot']=1500
  self.assertEqual(compare((current,[]),(conflict,[]))['conflicts'],['preceding-terminal-carry-evidence'])
  snapshot['archiveAttemptContext']['exportOperation']=self.operation(birth='134394066260992256')
  self.assertIsNone(compare((None,[row])))

 def checkpoint_wall_fixture(self, attempt=1, legacy=False):
  from log_lane_audit_selftest import OrdinaryExportSnapshotBoundaryTests
  available=self.availability()
  phases=dict(flush=0,steamDiscovery=15,cachedStateAndFileDiscovery=35)
  phases.update(dict(windowsSystemPowerReport=10000,remainingStateAndManifest=137) if legacy else
                dict(windowsReportAndRemainingState=10000,manifestWrite=137))
  available['exportPhaseTiming']=dict(elapsedMs=phases,evidenceBuildFailures=0,completedTickMs='10267030',
     scope='completed-pre-snapshot-phases;current-export-archive-and-terminal-not-yet-observed')
  snapshot=OrdinaryExportSnapshotBoundaryTests.snapshot(attempt)
  snapshot['archiveAttemptContext']['exportOperation']=self.operation()
  return available,snapshot

 def test_checkpoint_wall_validates_old_and_new_layouts_and_ticks(self):
  def bound(available,snapshot):
   _,audits=self.cross_view([(available,[])],snapshots={0:snapshot})
   return audits[0]['commonEvidence']['ordinaryExportSnapshotBoundary'].get('completedWallLowerBound')
  for legacy in (False,True):
   available,snapshot=self.checkpoint_wall_fixture(legacy=legacy)
   value=bound(available,snapshot)
   self.assertEqual((value['preSnapshotMs'],value['archiveThroughCheckpointMs'],value['lowerBoundMs']),(10187,1516,11703))
   self.assertEqual(value['terminalComparison']['state'],'no-comparable-terminal')
  available,snapshot=self.checkpoint_wall_fixture()
  wrong=copy.deepcopy(available);del wrong['exportPhaseTiming']['completedTickMs']
  self.assertEqual(bound(wrong,snapshot)['state'],'pre-snapshot-timing-unavailable')
  for state,changes in (
      ('pre-snapshot-timing-unsupported-or-incomplete',{'evidenceBuildFailures':1}),
      ('pre-snapshot-timing-unsupported-or-incomplete',{'evidenceBuildFailures':False}),
      ('pre-snapshot-timing-unsupported-or-incomplete',{'completedTickMs':'010267030'}),
      ('pre-snapshot-timing-unsupported-or-incomplete',{'completedTickMs':'18446744073709551616'}),
      ('pre-snapshot-ticks-conflict',{'completedTickMs':'10256842'}),
      ('pre-snapshot-ticks-conflict',{'completedTickMs':'10267031'})):
   with self.subTest(changes=changes):
    wrong=copy.deepcopy(available);wrong['exportPhaseTiming'].update(changes)
    value=bound(wrong,snapshot)
    self.assertEqual(value['state'],state);self.assertIsNone(value['lowerBoundMs'])
  for extras in ({'windowsSystemPowerReport':0},{'unknownPhase':0},{'availabilityAndDestination':0},{'steamDiscovery':True}):
   wrong=copy.deepcopy(available);wrong['exportPhaseTiming']['elapsedMs'].update(extras)
   self.assertIsNone(bound(wrong,snapshot)['lowerBoundMs'])
  snapshot['archiveAttemptContext']['exportOperation']=self.operation(birth='134394066260992256')
  self.assertIsNone(bound(available,snapshot))

 def test_checkpoint_wall_partitions_only_valid_overlapped_report_join(self):
  available,snapshot=self.checkpoint_wall_fixture()
  scheduling=dict(schema=1,scope='export-wall-intervals;may-overlap',mode='overlapped-task',
                  reportAdapterMs=10000,remainingStateMs=203,overlapMs=203,joinMs=9797)
  def bound(availability):
   _,audits=self.cross_view([(availability,[])],snapshots={0:snapshot})
   return audits[0]['commonEvidence']['ordinaryExportSnapshotBoundary']['completedWallLowerBound']
  availability=dict(available,windowsSleepScheduling=scheduling)
  result=bound(availability)
  self.assertEqual((result['lowerBoundMs'],result['windowsReportJoin']['elapsedMs'],
                    result['windowsReportJoin']['otherCompletedWallMs']),(11703,9797,1906))
  result=bound(dict(available,windowsSleepScheduling=dict(scheduling,joinMs=0)))
  self.assertEqual((result['windowsReportJoin']['elapsedMs'],result['windowsReportJoin']['otherCompletedWallMs']),
                   (0,11703))
  legacy,_=self.checkpoint_wall_fixture(legacy=True)
  inline=dict(scheduling,mode='inline-launch-fallback',reportAdapterMs=9000,overlapMs=0,joinMs=2)
  for rejected in (available,dict(available,windowsSleepScheduling=inline),
                   dict(available,windowsSleepScheduling=dict(scheduling,joinMs=10000)),
                   dict(legacy,windowsSleepScheduling=scheduling)):
   result=bound(rejected)
   self.assertEqual(result['lowerBoundMs'],11703)
   self.assertNotIn('windowsReportJoin',result)

 def test_checkpoint_wall_preserves_exact_terminal_and_rejects_shorter_or_other_writer(self):
  available,snapshot=self.checkpoint_wall_fixture()
  current,row=self.carrying_availability()
  row.update(attempts=1,exportElapsedMs=12000,archiveElapsedMs=1700,
             phaseElapsedMs=dict(available['exportPhaseTiming']['elapsedMs']))
  current['priorOrdinaryExportTerminal']['terminal'].update(attempts=1,exportElapsedMs=12000,archiveElapsedMs=1700,
            phaseElapsedMs=row['phaseElapsedMs'])
  def comparisons(local,others):
   cross,audits=self.cross_view([(available,local),*others],snapshots={0:snapshot})
   bound=audits[0]['commonEvidence']['ordinaryExportSnapshotBoundary']['completedWallLowerBound']
   return bound['terminalComparison'],cross['receipts'][0]['completedWallLowerBoundComparison']
  local,joined=comparisons([],[(None,[row]),(current,[])])
  self.assertEqual(local['state'],'no-comparable-terminal')
  self.assertEqual(joined['state'],'reported-timings-compatible')
  self.assertEqual(joined['terminalElapsedMsBySource'],dict.fromkeys(('native-terminal','preceding-terminal-carry'),
                        dict(exportElapsedMs=12000,archiveElapsedMs=1700)))
  short=dict(row,exportElapsedMs=11702,archiveElapsedMs=1515,
             phaseElapsedMs=dict(row['phaseElapsedMs'],steamDiscovery=14))
  local,joined=comparisons([short],[])
  self.assertEqual(local,joined)
  self.assertEqual(local['state'],'conflicting-report')
  self.assertEqual(local['checkpointLowerBoundMs'],11703)
  self.assertEqual(local['terminalElapsedMsBySource']['native-terminal'],dict(exportElapsedMs=11702,archiveElapsedMs=1515))
  self.assertEqual(local['conflicts'],['native-terminal-export-shorter-than-bound','native-terminal-archive-shorter-than-bound',
                                       'native-terminal-vs-pre-snapshot-phases'])
  foreign=dict(short,exportOperation=self.operation(birth='134394066260992256'),writerProcessCreation100ns='134394066260992256')
  self.assertEqual(comparisons([],[(None,[foreign])])[1]['state'],'no-comparable-terminal')

 def test_checkpoint_wall_join_share_requires_consistent_full_operation_timing(self):
  available,snapshot=self.checkpoint_wall_fixture()
  available['windowsSleepScheduling']=dict(schema=1,scope='export-wall-intervals;may-overlap',mode='overlapped-task',
                  reportAdapterMs=10000,remainingStateMs=203,overlapMs=203,joinMs=9797)
  current,row=self.carrying_availability()
  row.update(attempts=1,exportElapsedMs=12000,archiveElapsedMs=1700,
             phaseElapsedMs=dict(available['exportPhaseTiming']['elapsedMs']))
  carry=current['priorOrdinaryExportTerminal']['terminal']
  carry.update(attempts=1,exportElapsedMs=12000,archiveElapsedMs=1700,phaseElapsedMs=row['phaseElapsedMs'])
  def comparison(rows, carried=None):
   batches=[(available,[]),(None,rows)]
   if carried is not None:batches.append((carried,[]))
   cross,_=self.cross_view(batches,snapshots={0:snapshot})
   return cross['receipts'][0]['completedWallLowerBoundComparison']
  key='windowsReportJoinShareOfNativeExport'
  result=comparison([row],current)[key]
  self.assertEqual((result['joinMs'],result['nativeExportMs'],result['remainingNativeWallMs']),(9797,12000,2203))
  self.assertEqual(result['agreeingSources'],['native-terminal','preceding-terminal-carry'])
  self.assertEqual(comparison([],current)[key]['agreeingSources'],['preceding-terminal-carry'])
  self.assertNotIn(key,comparison([]))
  self.assertNotIn(key,comparison([row,row],current))
  for phases in ({k:v for k,v in row['phaseElapsedMs'].items() if k!='windowsReportAndRemainingState'},
                 dict(row['phaseElapsedMs'],windowsReportAndRemainingState=9999)):
   self.assertNotIn(key,comparison([dict(row,phaseElapsedMs=phases)]))
  disagreement=copy.deepcopy(current)
  disagreement['priorOrdinaryExportTerminal']['terminal']['exportElapsedMs']=12100
  self.assertNotIn(key,comparison([row],disagreement))

 def test_checkpoint_wall_retry_total_includes_delay_once_and_requires_same_checkpoint_summary(self):
  available,snapshot=self.checkpoint_wall_fixture(attempt=2)
  first=snapshot['archiveAttemptTiming']['attempts'][0]
  first['completedStepElapsedMs']['retryDelay']=50
  first['totalElapsedMs']+=50
  def bound():
   _,audits=self.cross_view([(available,[])],snapshots={0:snapshot})
   return audits[0]['commonEvidence']['ordinaryExportSnapshotBoundary']['completedWallLowerBound']
  value=bound()
  self.assertEqual(value['lowerBoundMs'],11703)
  self.assertIsNone(value['firstAttemptTotalIncludingRetryDelayMs'])
  snapshot['previousArchiveAttempt']=dict(schema=1,scope='previous-attempt-only;not-current-export-outcome',
       attempt=1,reason='tar-exit-nonzero',win32Error=1,temporaryRemoveError=0,stagingRemoveError=0,cleanupOk=True)
  value=bound()
  self.assertEqual(value['firstAttemptTotalIncludingRetryDelayMs'],first['totalElapsedMs'])
  self.assertEqual(value['lowerBoundMs'],11703+first['totalElapsedMs'])

 def test_snapshot_call_work_is_preserved_and_conflicts_only_with_itself(self):
  from log_lane_audit_selftest import ArchiveAttemptTimingTests, ArchiveSnapshotWorkTests
  timing=ArchiveAttemptTimingTests.receipt()
  work=ArchiveSnapshotWorkTests.work()
  work['rootInspection'].update(enteredCalls=2,returnedCalls=2,elapsedMs=5,maxElapsedMs=4,maxSourceOrdinal=1)
  timing['attempts'][0]['snapshotWork']=work
  row=dict(self.terminal(),archiveAttemptTiming=timing)
  batch=[(self.availability(),[]),(None,[row])]
  cross,_=self.cross_view(batch);view=cross['receipts'][0]
  observed=view['archiveSnapshotWork']['attempts'][0]
  self.assertEqual(observed['state'],'reported-snapshot-work')
  self.assertEqual(observed['calls']['rootInspection']['reported']['elapsedMs'],5)
  self.assertIsNone(observed['calls']['steamCopy']['reported']['elapsedMs'])
  self.assertEqual(view['archiveTiming']['attempts'][0]['completedStepElapsedMs']['sourceSnapshot'],7)
  different=copy.deepcopy(row)
  different['archiveAttemptTiming']['attempts'][0]['snapshotWork']['rootInspection']['elapsedMs']=6
  legacy=copy.deepcopy(row)
  del legacy['archiveAttemptTiming']['attempts'][0]['snapshotWork']
  for other in (different,legacy):
   with self.subTest(other=other):
    cross,_=self.cross_view(batch+[(None,[other])]);view=cross['receipts'][0]
    self.assertTrue(view['differentArchiveSnapshotWorkProjections'])
    self.assertIsNone(view['archiveSnapshotWork'])
    self.assertFalse(view['differentArchiveTimingProjections'])
    self.assertIsNotNone(view['archiveTiming'])
    self.assertTrue(view['terminal']['reported']['ok'])

 @staticmethod
 def output_check(timing, temporary, final):
  timing['attempts'][0]['outputCheckEvidence']=dict(schema=1,
      scope='original-archive-output-checks;not-zip-integrity-or-current-path-proof',temporary=temporary,final=final)

 @staticmethod
 def output_state(state, size=None, error=None, category=None):
  return dict(state=state,sizeBytes=size,filesystemErrorValue=error,filesystemErrorCategory=category)

 def test_output_checks_preserve_empty_missing_and_failed_open(self):
  from log_lane_audit_selftest import ArchiveAttemptTimingTests
  unentered=self.output_state('not-entered')
  for temp,final in ((self.output_state('empty-file',0),unentered),
                    (self.output_state('file-size-error',error=5,category='system'),unentered),
                    (self.output_state('stream-opened-nonempty',20),self.output_state('stream-open-failed',20))):
   with self.subTest(temp=temp,final=final):
    timing=ArchiveAttemptTimingTests.receipt();self.output_check(timing,temp,final)
    row=dict(self.terminal(),archiveAttemptTiming=timing)
    local=self.view([row])['terminal']['archiveTiming']['attempts'][0]['outputCheck']
    self.assertEqual(local['state'],'reported-original-output-checks')
    self.assertEqual(local['checks'],dict(temporary=temp,final=final))
    cross,_=self.cross_view([(self.availability(),[]),(None,[row])]);result=cross['receipts'][0]
    self.assertEqual(result['archiveChecks']['attempts'][0]['outputCheck'],local)

 def test_output_check_conflict_does_not_hide_valid_timing(self):
  from log_lane_audit_selftest import ArchiveAttemptTimingTests
  timing=ArchiveAttemptTimingTests.receipt()
  self.output_check(timing,self.output_state('empty-file',0),self.output_state('not-entered'))
  row=dict(self.terminal(),archiveAttemptTiming=timing)
  other=copy.deepcopy(row);other['archiveAttemptTiming']['attempts'][0]['outputCheckEvidence']['temporary']=self.output_state('not-regular-file')
  cross,_=self.cross_view([(self.availability(),[]),(None,[row]),(None,[other])]);result=cross['receipts'][0]
  self.assertTrue(result['differentArchiveCheckProjections']);self.assertIsNone(result['archiveChecks'])
  self.assertFalse(result['differentArchiveTimingProjections']);self.assertIsNotNone(result['archiveTiming'])

 def test_final_sharing_recheck_preserves_first_failure_and_rejects_inconsistent_evidence(self):
  from log_lane_audit_selftest import ArchiveAttemptTimingTests
  timing=ArchiveAttemptTimingTests.receipt()
  opened=self.output_state('stream-opened-nonempty',20)
  first=dict(self.output_state('stream-open-failed',20),openWin32Error=32)
  self.output_check(timing,opened,opened)
  attempt=timing['attempts'][0]
  check=attempt['outputCheckEvidence']
  check.update(firstFinalSharingCheck=first,finalSharingWaitMs=47)
  attempt['promotionEvidence']=dict(schema=1,scope='original-MoveFileExW-and-post-promotion-validation;not-current-path-or-unique-zip-proof',replacementRequested=False,currentPathAbsenceProven=False,uniqueFinalZipProven=False,promotion=dict(observed=True,accepted=True,win32Error=0),postPromotionValidation=dict(entered=True,returned=True,readableAndNonempty=True))
  row=dict(self.terminal(),archiveAttemptTiming=timing)
  def read_output():return self.view([row])['terminal']['archiveTiming']['attempts'][0]['outputCheck']
  result=read_output()
  self.assertEqual(result['checks']['final'],opened)
  self.assertEqual(result['finalSharingRecheck'],dict(firstCheck=first,waitElapsedMs=47))
  for field, bad in [('firstFinalSharingCheck',dict(first,openWin32Error=5)),('finalSharingWaitMs',True)]:
   with self.subTest(field=field):
    good=check[field];check[field]=bad
    result=read_output()
    self.assertEqual(result['state'],'invalid-fields');self.assertNotIn('finalSharingRecheck',result)
    check[field]=good

 def test_temporary_sharing_recheck_uses_only_the_first_attempt_repack_wait(self):
  from log_lane_audit_selftest import ArchiveAttemptTimingTests
  from log_lane_audit import archive_output_check_record, archive_promotion_record
  timing=ArchiveAttemptTimingTests.receipt();attempt=timing['attempts'][0]
  opened=self.output_state('stream-opened-nonempty',20)
  blocked=dict(self.output_state('stream-open-failed',20),openWin32Error=32)
  self.output_check(timing,opened,opened)
  check=attempt['outputCheckEvidence']
  check.update(firstTemporarySharingCheck=blocked,temporarySharingWaitMs=48)
  phases=attempt['completedStepElapsedMs'];phases['archiveValidationAndPromotion']=48
  attempt['totalElapsedMs']+=48
  move=attempt['promotionEvidence']=dict(schema=1,
      scope='original-MoveFileExW-and-post-promotion-validation;not-current-path-or-unique-zip-proof',
      replacementRequested=False,currentPathAbsenceProven=False,uniqueFinalZipProven=False,
      promotion=dict(observed=True,accepted=True,win32Error=0),
      postPromotionValidation=dict(entered=True,returned=True,readableAndNonempty=True))
  def projected(part):return archive_output_check_record(part,archive_promotion_record(part))
  result=projected(attempt)
  self.assertEqual(result['checks']['temporary'],opened)
  self.assertEqual(result['temporarySharingRecheck'],dict(firstCheck=blocked,waitElapsedMs=48))
  cross,_=self.cross_view([(self.availability(),[]),(None,[dict(self.terminal(),archiveAttemptTiming=timing)])])
  self.assertEqual(cross['receipts'][0]['archiveChecks']['attempts'][0]['outputCheck'],result)
  failed=copy.deepcopy(attempt)
  failed['outputCheckEvidence'].update(temporary=blocked,final=self.output_state('not-entered'))
  failed['promotionEvidence'].update(promotion=dict(observed=False,accepted=None,win32Error=None),
      postPromotionValidation=dict(entered=False,returned=None,readableAndNonempty=None))
  self.assertEqual(projected(failed)['temporarySharingRecheck'],result['temporarySharingRecheck'])
  cases=[dict(attempt,attempt=2),dict(attempt,attemptBodyReturned=False),
         dict(attempt,completedStepElapsedMs=dict(phases,retryDelay=50)),
         dict(attempt,completedStepElapsedMs=dict(phases,archiveValidationAndPromotion=47)),
         dict(attempt,promotionEvidence=dict(move,sharingRetry=dict(firstWin32Error=32,waitElapsedMs=23,
                                                                     secondWin32Error=32,secondWaitElapsedMs=24))),
         dict(attempt,outputCheckEvidence=dict(check,temporarySharingWaitMs=True)),
         dict(attempt,outputCheckEvidence=dict(check,firstTemporarySharingCheck=dict(blocked,openWin32Error=5)))]
  for part in cases:
   with self.subTest(part=part):
    result=projected(part)
    self.assertEqual(result['state'],'invalid-fields');self.assertNotIn('temporarySharingRecheck',result)

 def test_same_candidate_sharing_retry_is_separate_from_last_move_and_repack(self):
  from log_lane_audit_selftest import ArchiveAttemptTimingTests
  timing=ArchiveAttemptTimingTests.receipt();attempt=timing['attempts'][0]
  promotion=attempt['promotionEvidence']=dict(schema=1,
      scope='original-MoveFileExW-and-post-promotion-validation;not-current-path-or-unique-zip-proof',
      replacementRequested=False,currentPathAbsenceProven=False,uniqueFinalZipProven=False,
      promotion=dict(observed=True,accepted=True,win32Error=0),
      postPromotionValidation=dict(entered=True,returned=True,readableAndNonempty=True))
  self.assertNotIn('sharingRetry',ArchiveAttemptTimingTests.view(timing)['attempts'][0]['promotion'])
  promotion['sharingRetry']=dict(firstWin32Error=32,waitElapsedMs=47)
  row=dict(self.terminal(),attempts=1,archiveAttemptTiming=timing)
  cross,_=self.cross_view([(self.availability(),[]),(None,[row])]);result=cross['receipts'][0]
  retained=result['archiveChecks']['attempts'][0]['promotion']
  self.assertEqual(retained['sharingRetry']['firstWin32Error'],32)
  self.assertEqual(retained['sharingRetry']['waitElapsedMs'],47)
  self.assertTrue(retained['promotion']['accepted']);self.assertEqual(result['terminal']['reported']['attempts'],1)
  first=dict(firstWin32Error=32,waitElapsedMs=47)
  self.assertNotIn('secondWin32Error',retained['sharingRetry'])
  second=dict(secondWin32Error=32,secondWaitElapsedMs=62)
  promotion['sharingRetry']=dict(first,**second)
  attempt['completedStepElapsedMs']['archiveValidationAndPromotion']=109
  attempt['totalElapsedMs']+=109
  cross,_=self.cross_view([(self.availability(),[]),(None,[row])])
  retained=cross['receipts'][0]['archiveChecks']['attempts'][0]['promotion']
  self.assertEqual(retained['state'],'reported-original-promotion-observations')
  self.assertEqual({key:retained['sharingRetry'][key] for key in second},second)
  self.assertEqual(retained['promotion']['win32Error'],0)
  attempt['completedStepElapsedMs']['retryDelay']=62
  attempt['totalElapsedMs']+=62
  retained=ArchiveAttemptTimingTests.view(timing)['attempts'][0]['promotion']
  self.assertEqual(retained['state'],'invalid-fields')
  self.assertIn('sharingRetry:second-wait-conflicts-with-repack-delay',retained['fieldIssues'])
  del attempt['completedStepElapsedMs']['retryDelay']
  attempt['totalElapsedMs']-=62
  for fields in (dict(secondWin32Error=32),dict(secondWaitElapsedMs=62),dict(second,secondWin32Error=5),dict(second,secondWaitElapsedMs=True)):
   with self.subTest(fields=fields):
    promotion['sharingRetry']=dict(first,**fields)
    retained=ArchiveAttemptTimingTests.view(timing)['attempts'][0]['promotion']
    self.assertEqual(retained['state'],'invalid-fields')
    self.assertEqual(retained['sharingRetry']['firstWin32Error'],32)
    self.assertNotIn('secondWin32Error',retained['sharingRetry'])
  for observation in (dict(firstWin32Error=5,waitElapsedMs=47),dict(firstWin32Error=32,waitElapsedMs=True),None):
   promotion['sharingRetry']=observation
   retained=ArchiveAttemptTimingTests.view(timing)['attempts'][0]['promotion']
   self.assertNotIn('sharingRetry',retained);self.assertEqual(retained['state'],'invalid-fields')

 def test_malformed_or_contradictory_output_fields_are_not_adopted(self):
  from log_lane_audit_selftest import ArchiveAttemptTimingTests
  for temp,final in ((self.output_state('empty-file',False),self.output_state('not-entered')),
                    (self.output_state('stream-open-failed',20,5,'system'),self.output_state('not-entered')),
                    (self.output_state('empty-file',0),self.output_state('stream-opened-nonempty',20))):
   timing=ArchiveAttemptTimingTests.receipt();self.output_check(timing,temp,final)
   result=ArchiveAttemptTimingTests.view(timing)
   check=result['attempts'][0]['outputCheck']
   self.assertEqual(check['state'],'invalid-fields');self.assertEqual(check['checks'],{})
   self.assertEqual(result['state'],'reported-completed-steps')

if __name__=='__main__':unittest.main()
