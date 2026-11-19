// Current fixed-capture semantics only. No preparation, process launch, or source approval.
import fs from 'node:fs';import assert from 'node:assert/strict';import {createHash} from 'node:crypto';
import {currentExpected,normalizedCurrentTrace,verifyCurrentSemantics,verifyCurrentChildren} from './r3-current105-oracle.mjs';
import {observeCurrentOrderedReport} from './r3-current105-ordered-observation.mjs';
import {verifyCurrent105OriginalGroups} from './r3-current105-original-groups.mjs';
import {verifyReviewedDisposeBranches} from './r3-reviewed-contract.mjs';
import {verifyOwnershipReturn} from './r3-current-ownership.mjs';
const hash=b=>createHash('sha256').update(b).digest('hex'),bytes=fs.readFileSync(new URL('./r3-terminal26-current-negative-expectations.json',import.meta.url));
assert.equal(hash(bytes),'efe30084c1436c7c32bbac058b1ce4ec8da6c8c9f1074fca02247660b1a2843b','CURRENT26_FIXED_EXPECTATIONS_CHANGED');
const spec=JSON.parse(bytes),roles=new Set(['positive','cancellation','psOwnership','psBinding']);
assert(spec.schemaVersion===1&&spec.kind==='CURRENT_CAPTURE_TERMINAL26_SOURCE_DERIVED_NEGATIVE_EXPECTATIONS_V1');
function options(role,value){
 assert(roles.has(role),'CURRENT26_UNKNOWN_PIPE_ROLE');
 assert(value&&Object.keys(value).sort().join('|')==='managedInputContract|mutation|nativeSourceProfile|sourceCaptureManifestSha256','CURRENT26_EXACT_SCOPE_OPTIONS_REQUIRED');
 assert(value.managedInputContract===spec.managedInputContract&&value.nativeSourceProfile===spec.nativeSourceProfile&&value.sourceCaptureManifestSha256===spec.sourceCaptureManifestSha256,'CURRENT26_FIXED_SOURCE_SCOPE_MISMATCH');
 assert.equal(value.mutation,role==='positive'?null:spec.expectations[role].mutation,'CURRENT26_EXACT_MUTATION_REQUIRED');return value;
}
export function current26FixedExpected(role,value){options(role,value);return structuredClone(role==='positive'?currentExpected('positive',{managedInputContract:spec.managedInputContract}):spec.expectations[role]);}
export function verifyCurrent26FixedSemantics(role,raw,value){
 options(role,value);const e=current26FixedExpected(role,value);
 if(role==='positive'){
  const existing=verifyCurrentSemantics('positive',raw,{managedInputContract:spec.managedInputContract});
  verifyCurrent105OriginalGroups(raw,{currentRogOffExecution:true,retainedWakeHideRetry:true,managedInputContract:spec.managedInputContract});
  return {role,cases:raw.cases.length,checks:raw.checks,exactFailureCount:0,originalSemanticResult:existing,sourceScope:spec.nativeSourceProfile,sourceAuthorityGranted:false,wholeParentAccepted:false};
 }
 assert(raw&&raw.status==='FAIL'&&raw.forbiddenIo===0&&raw.fullTranslationUnit===true&&raw.normalProductMainExecuted===false&&raw.realDeviceIo===false&&!raw.exception,'CURRENT26_COMPLETE_OFFLINE_NEGATIVE_REQUIRED');
 assert.equal(raw.checks,e.checks);assert.equal(raw.cases.length,e.cases);assert.equal(new Set(raw.cases.map(row=>row.name)).size,e.uniqueNames);
 observeCurrentOrderedReport(raw,{managedInputContract:spec.managedInputContract});const trace=normalizedCurrentTrace(raw),shape=trace.map(row=>[row.sourceOrdinal,row.nameOccurrence,row.case,row.assertion,row.assertionOccurrence]);
 assert.equal(hash(JSON.stringify(shape)),e.traceIdentitySha256,'CURRENT26_COMPLETE_ORDERED_TRACE_MISMATCH');assert(trace.every(row=>typeof row.passed==='boolean'));
 const failures=new Set(e.exactFailures.map(row=>row.id));assert.equal(failures.size,e.exactFailures.length);assert.deepEqual(trace.filter(row=>!row.passed).map(row=>row.id).sort(),[...failures].sort(),'CURRENT26_EXACT_FAILURE_SET_MISMATCH');
 for(const row of trace)assert.equal(row.passed,!failures.has(row.id),'CURRENT26_UNEXPECTED_ASSERTION_RESULT');for(const wanted of e.requiredOriginalPassingInstances)assert(trace.some(row=>row.id===wanted.id&&row.passed),'CURRENT26_REQUIRED_ORIGINAL_PASS_MISSING');
 const before=raw.orderedReturn.preTargetReturnBody,after=raw.orderedReturn.postTargetReturnTail;
 for(const [actual,wanted]of [[before,e.boundaries.pre],[after,e.boundaries.post]]){
  assert.equal(actual.afterSourceOrdinal,wanted.ordinal);assert.equal(actual.checks,wanted.checks);assert.equal(actual.lastCaseName,wanted.lastCaseName);assert.equal(actual.resources.msiModeDebt,wanted.msiModeDebt);
  for(const field of ['hostHandle','pipeHandle','pipeConnected','prepared','quarantined','visibility','psRetirement','msiSdDebt','workRequested','workInFlight'])assert.equal(actual.resources[field],false,'CURRENT26_RESOURCE_NOT_RETURNED:'+field);
  assert.equal(actual.resources.childObservers,0);assert.equal(actual.resources.workQueue,0);
 }
 assert(Number.isSafeInteger(before.resources.msiModeDebtEpisode)&&before.resources.msiModeDebtEpisode>0);assert.equal(after.resources.msiModeDebtEpisode,before.resources.msiModeDebtEpisode+1);
 const debt=raw.cases.at(-1).retainedDebtProof;assert(debt&&debt.returnDebtEpisodeId===after.resources.msiModeDebtEpisode&&debt.retained.length>0&&debt.retained.every(row=>row.returnDebtRetained===true)&&debt.discharged.length===0,'CURRENT26_FINAL_MSI_DEBT_WITNESS_REQUIRED');assert.equal(after.resources.spawnCount,e.expectedOwnedChildren);
 verifyReviewedDisposeBranches(raw);verifyOwnershipReturn(raw,role==='psOwnership');
 if(role==='cancellation'){
  assert.equal(e.exactFailures.length,15);assert.equal(e.requiredOriginalPassingInstances.length,14);assert.equal(new Set([...e.exactFailures,...e.requiredOriginalPassingInstances].map(row=>row.id)).size,29);
  for(const kind of ['off','exit','power','generation','persona','reentry']){
   const matches=raw.cases.filter(row=>row.psWakeCancellation?.kind===kind);assert.equal(matches.length,1);const row=matches[0],q=row.psWakeCancellation;
   assert(q.callbackExecuted===true&&q.probeCalls===(kind==='generation'?2:1)&&q.markedRebuilt===false&&q.rearmRequested===false,'CURRENT26_ORIGINAL_CALLBACK_PROBE_REQUIRED');
   assert.equal(q.recreateCommandsAdded,['exit','generation','reentry'].includes(kind)?0:1);assert(row.nativeEvents.some(event=>event.event==='ps-wake-rearm-deferred'&&event.detail.reason==='admission-changed-before-rebuild'&&event.detail.queryRequestId===q.requestIdBefore&&event.detail.currentRequestId===q.requestIdAfter));if(kind==='exit')assert.equal(q.directRecreateReturned,false);
  }
 }
 verifyCurrent105OriginalGroups(raw,{ownershipRollback:role==='psOwnership',bindingRollback:['psBinding','cancellation'].includes(role),cancellationRollback:role==='cancellation',currentRogOffExecution:true,retainedWakeHideRetry:true,managedInputContract:spec.managedInputContract});
 return {role,cases:raw.cases.length,checks:raw.checks,exactFailureCount:e.exactFailures.length,requiredOriginalPassCount:e.requiredOriginalPassingInstances.length,completeOrderedTraceMatched:true,allDetailedGroupsMatched:true,sourceScope:spec.nativeSourceProfile,sourceAuthorityGranted:false,wholeParentAccepted:false};
}
export function verifyCurrent26FixedChildren(role,raw,managed,nativePid,value){
 options(role,value);const e=current26FixedExpected(role,value);const common=verifyCurrentChildren('positive',raw,managed,nativePid,{managedInputContract:spec.managedInputContract});
 assert.equal(common.ownedChildren,e.expectedOwnedChildren);return {actualRole:role,commonBindingScope:'Exact124 source/assembly/parent/nonce/exit obligations shared by all four pipe roles; no positive semantic result borrowed.',common,sourceAuthorityGranted:false,wholeParentAccepted:false};
}
