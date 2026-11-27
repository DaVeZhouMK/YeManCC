// Read-only interpretation of original MSI fixture observations; never grants return authority.
import assert from 'node:assert/strict';
const requiredReturnPoints=['msiReturnBeforeTakeover','msiReturnAfterVerifiedReturn','msiReturnAtFixtureEnd'];
const hasObservation=(row,point)=>Object.hasOwn(row,point)&&row[point]!=null;
export function inspectMsiReturnObservation(value){
 const unknown={status:'UNKNOWN',logicalOffEligibility:null,startSeamActive:null,fullOwnershipReturnProven:false,durableDischargeProven:false};
 if(value===undefined||value===null)return unknown;
 assert(value&&typeof value==='object'&&!Array.isArray(value)&&value.schemaVersion===1,'MSI_RETURN_OBSERVATION_SCHEMA_REQUIRED');
 assert.equal(value.observationIsReturnProof,false,'MSI_RETURN_SNAPSHOT_CANNOT_GRANT_PROOF');
 assert.equal(value.coherentSnapshot,false,'MSI_RETURN_CROSS_LOCK_SNAPSHOT_NOT_COHERENT');
 assert.equal(value.durableDischargeProven,null,'MSI_RETURN_DIAGNOSTIC_CANNOT_SIGN_DURABLE');
 assert.equal(typeof value.modeReturnOwed,'boolean','MSI_RETURN_OWED_BOOLEAN_REQUIRED');
 assert.equal(typeof value.startSeamActive,'boolean','MSI_RETURN_SEAM_BOOLEAN_REQUIRED');
 assert.equal(value.durableDischargeUnavailableInStartSeam,value.startSeamActive,'MSI_RETURN_SEAM_CONTRACT_REQUIRED');
 if(value.personaObserved===true){assert.equal(typeof value.logicalConfiguredPersona,'string');assert.equal(typeof value.operationalTargetEnabled,'boolean');assert.equal(value.logicalOffEligibility,!value.operationalTargetEnabled&&value.logicalConfiguredPersona==='disabled','MSI_RETURN_ACTUAL_OFF_ELIGIBILITY_REQUIRED');}
 const stable=value.modeDebtReadStable===true;
 const pending=value.ownerMetadataObserved===true&&(value.owner?.pending===true||value.owner?.working===true)||value.storeMetadataObserved===true&&value.store?.modeReturnPending===true;
 return {...unknown,status:!stable?'UNKNOWN':value.modeReturnOwed?'MODE_RETURN_STILL_OWED':pending?'RECORDED_RETURN_STILL_PENDING':'NO_ATOMIC_MODE_DEBT_OBSERVED',
  logicalOffEligibility:value.personaObserved===true?value.logicalOffEligibility:null,startSeamActive:value.startSeamActive,
  ownerMetadataObserved:value.ownerMetadataObserved===true,storeMetadataObserved:value.storeMetadataObserved===true};
}
export function inspectMsiReturnCases(cases,{required=false}={}){
 assert(Array.isArray(cases)&&typeof required==='boolean','MSI_RETURN_CASE_ARRAY_REQUIRED');
 const names=['MSI/31000/same-intent-external-return-during-pending-start','MSI/31000/same-intent-external-return-during-success-settle'];
 const points=['msiReturnBeforeTakeover','msiReturnAfterVerifiedReturn','msiReturnBeforeCleanup','msiReturnAfterCleanup','msiReturnAtFixtureEnd'];
 const selected=cases.filter(row=>names.includes(row.name));if(required){assert.equal(selected.length,2,'MSI_RETURN_TWO_ORIGINAL_CASES_REQUIRED');assert.equal(new Set(selected.map(row=>row.name)).size,2,'MSI_RETURN_TWO_DISTINCT_ORIGINAL_CASES_REQUIRED');}
 let present=false;
 const rows=selected.map(row=>{
  if(required)for(const point of requiredReturnPoints)assert(hasObservation(row,point),'MSI_RETURN_DECLARED_OBSERVATION_MISSING:'+point);
  const observations={};
  for(const point of points)if(Object.hasOwn(row,point)){present=true;observations[point]=inspectMsiReturnObservation(row[point]);}
  return {case:row.name,observations,fullOwnershipReturnProven:false};
 });
 return {present,required,cases:rows,fullOwnershipReturnProven:false,durableDischargeProven:false};
}

// Explicit parameter consumption over actual raw evidence. This does not run a
// producer, qualify compiled/current source or replace a complete parent mode.
export function inspectMsiCancellationCases(raw,{boundary,phase}={}){
 const need=(value,code)=>assert(value,'MSI_CANCELLATION_'+code);
 need(['clock','off','new-request','intent','exit'].includes(boundary),'KNOWN_PARAMETER_REQUIRED');
 need(boundary==='exit'?['pending','success'].includes(phase):phase===undefined,'EXACT_PHASE_SCOPE_REQUIRED');
 need(raw?.status==='PASS'&&raw.forbiddenIo===0&&raw.fullTranslationUnit===true&&
  raw.normalProductMainExecuted===false&&raw.realDeviceIo===false&&
  Array.isArray(raw.failures)&&raw.failures.length===0,'ACTUAL_POSITIVE_OFFLINE_REPORT_REQUIRED');
 const names=['MSI/31000/same-intent-external-return-during-pending-start','MSI/31000/same-intent-external-return-during-success-settle'];
 const expected=phase?[names[phase==='pending'?0:1]]:names;
 need(Array.isArray(raw.cases)&&raw.cases.length===expected.length&&
  raw.cases.every((row,index)=>row?.name===expected[index]&&row.sourceOrdinal===index+1&&row.nameOccurrence===1),
  'EXACT_ORIGINAL_PARAMETER_CASES_REQUIRED');
 need(Number.isSafeInteger(raw.checks)&&raw.checks>0&&Array.isArray(raw.assertionTrace)&&
  raw.assertionTrace.length===raw.checks,'COMPLETE_ASSERTION_TRACE_REQUIRED');
 for(const [index,row]of raw.assertionTrace.entries())need(row?.passed===true&&row.checkIndex===index+1&&
  raw.cases.some(item=>item.name===row.case&&item.sourceOrdinal===row.sourceOrdinal&&item.nameOccurrence===row.nameOccurrence),
  'ASSERTION_TRACE_CONFLICT');
 const integer=value=>Number.isSafeInteger(value)&&value>=0;
 const rows=raw.cases.map(item=>{
  const input=item.jointCancellationInput,result=item.jointCancellationResult,actual=result?.actualReturnResult;
  need(item.policyBoundary?.kind===boundary&&item.policyBoundary.hookFired===true&&
   input?.stage==='actual-command-open-before-original-durable-mode-write'&&
   input.startSeamActive===false&&input.forwardPermissionAfter===false,'ACTUAL_NONSEAM_INPUT_REQUIRED');
  for(const key of ['requestBefore','requestAfter','scopeRequest','scopePower','scopeDeadline','clockBefore','clockAfter','intentBefore','intentAfter','spawnsBefore'])
   need(integer(input[key]),'TYPED_INPUT_REQUIRED');
  need(input.requestBefore>0&&input.scopeRequest===input.requestBefore&&input.scopePower>0&&
   input.clockBefore<input.scopeDeadline&&input.clockAfter>=input.clockBefore,'LIVE_ORIGINAL_SCOPE_REQUIRED');
  need(result?.boundary===boundary&&result.newHostSpawns===0&&result.lateForwardPermission===false&&
   result.lateForwardPermissionAfterCleanup===false&&result.cleanupComplete===true&&
   Array.isArray(result.cleanupQueue)&&result.cleanupQueue.length===0,'LATE_PERMISSION_AND_RETURN_REQUIRED');
  need(actual?.stage==='result'&&actual.sourceScope==='existing-external-priority-return'&&
   actual.requestId===input.requestBefore&&actual.powerGeneration===input.scopePower&&
   actual.executionDeadline===input.scopeDeadline&&actual.startReturned===false&&
   actual.newPlayerAdmission===false&&actual.originalPlayerDeadlineRetained===true&&
   integer(actual.originalSignal)&&actual.originalSignal<=input.clockBefore&&
   actual.executionDeadline>actual.originalSignal&&actual.executionDeadline-actual.originalSignal<=3000,
   'ORIGINAL_NEGATIVE_START_RECEIPT_REQUIRED');
  const assertions=raw.assertionTrace.filter(row=>row.case===item.name).map(row=>row.assertion);
  const required=[
   'MSI-joint-cancellation-input-reaches-actual-nonseam-policy-return',
   'MSI-joint-cancellation-input-has-live-original-forward-permission-before-change',
   'MSI-joint-selected-cancellation-revokes-original-forward-permission-at-input',
   'MSI-joint-selected-cancellation-must-actually-fire-once-on-current-path',
   'MSI-joint-cancelled-original-return-has-one-real-negative-Start-result',
   'MSI-joint-late-old-permission-cannot-create-Host-or-new-mode2-duty',
   'real returned restores the original execution scope tuple',
   'MSI-joint-cancellation-returns-all-owned-work-before-independent-next-case'];
  if(boundary==='exit'){
   const owner=result.actualExitOwner;
   need(owner?.phase===phase&&['exitRequested','fenceRaised','captureProducerReturned','captureOwnershipReturned','HostLocalOwnershipReturned']
    .every(key=>owner[key]===true),'ACTUAL_CAPTURE_AND_HOST_EXIT_OWNER_REQUIRED');
   required.push('MSI-joint-exit-fence-remains-raised-without-new-player-admission',
    'MSI-joint-exit-runs-original-capture-retirement-and-proves-producer-return',
    'MSI-joint-exit-original-owner-attempts-app-exit-stop',
    'MSI-joint-exit-original-owner-returns-process-primary-and-pipe');
   need(input.requestAfter===input.requestBefore,'EXIT_MUST_NOT_ADMIT_A_NEW_REQUEST');
  }else{
   need(result.finalPhase==='success','OFF_VERIFIER_COMPLETION_REQUIRED');
   required.push('MSI-joint-cancellation-OFF-completes-original-verifier-before-next-case');
   if(boundary==='off'||boundary==='new-request'){
    need(input.requestAfter!==input.requestBefore&&item.policyBoundary.requestId===input.requestAfter&&
     item.boundarySettingsReceipt?.ok===true,'NEW_SETTINGS_REQUEST_REQUIRED');
    required.push(boundary==='off'?'MSI-joint-admitted-OFF-owns-new-request-and-old-return-cannot-resurrect-output':
     'MSI-joint-new-request-retains-own-persona-and-is-not-executed-by-old-return');
    if(boundary==='new-request')need(result.cleanupAdmissionAdvanceMs===30000,'NEW_REQUEST_PROTECTION_RETAINED');
   }else{
    need(input.requestAfter===input.requestBefore,'ORIGINAL_REQUEST_RETAINED');
    required.push('MSI-joint-clock-or-publication-revocation-keeps-original-player-and-zero-fallback');
    if(boundary==='clock'){
     need(input.clockAfter-input.clockBefore===3500&&input.clockAfter>=input.scopeDeadline,'ORIGINAL_WINDOW_EXPIRED');
     required.push('MSI-joint-clock-input-exceeds-only-original-policy-window-without-renewal');
    }else{
     need(input.intentAfter>input.intentBefore,'ORIGINAL_PUBLICATION_CHANGED');
     required.push('MSI-joint-publication-refresh-revokes-old-forward-scope');
    }
   }
  }
  need(required.every(name=>assertions.includes(name)),'NECESSARY_ASSERTION_MISSING');
  for(const point of requiredReturnPoints){
   need(hasObservation(item,point),'ORIGINAL_OBSERVATION_MISSING:'+point);inspectMsiReturnObservation(item[point]);
  }
  return {case:item.name,sourceOrdinal:item.sourceOrdinal,assertions:assertions.length,boundary,phase:phase??null,
   currentPathInputObserved:true,oldForwardPermissionRevoked:true,localCleanupObserved:true};
 });
 return {kind:'EXPLICIT_MSI_PARAMETER_SEMANTIC_REFERENCE_V1',boundary,phase:phase??null,
  cases:rows,scopedChecks:raw.checks,parameterSemanticsVerified:true,
  fullNativeModeCoverageVerified:false,fullParentVerified:false,currentPrimaryAccepted:false,
  executedByInspection:false,fullOwnershipReturnProven:false,durableDischargeProven:false,
  scope:'parameter raw and assertions only; compiled-source, process execution and physical authority remain separate'};
}
