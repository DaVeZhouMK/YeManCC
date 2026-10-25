import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {extract} from '../../../fixtures/legion_motion_consumer/extract_main.mjs';
const need=(ok,code)=>{if(!ok)throw Error('R3_REVIEWED_'+code);};
const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const digest=text=>crypto.createHash('sha256').update(text).digest('hex');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8'));
export const REVIEWED=read(new URL('./r3-reviewed-contract.json',import.meta.url));
export const REVIEWED_SPEC=read(new URL('./r3-reviewed-scenarios.json',import.meta.url));
export const LEGACY_SPEC=read(new URL('./r3-scenarios.json',import.meta.url));
const equal=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
const pair=rows=>rows.map(({case:c,assertion})=>JSON.stringify([c,assertion])).sort();
function reviewIdentity(){
 need(hash(new URL('./r3-reviewed-scenarios.json',import.meta.url))===REVIEWED.reviewedScenarioSha256,'SCENARIO_REVIEW_IDENTITY');
 need(hash(new URL('./r3-scenarios.json',import.meta.url))===REVIEWED.legacyScenarioSha256,'LEGACY_SCENARIO_CHANGED');
 for(const row of Object.values(REVIEWED.reviewedEvidence))need(hash(new URL('./'+row.file,import.meta.url))===row.sha256,'REVIEW_EVIDENCE_IDENTITY');
 need(REVIEWED_SPEC.sourceMainSha256===REVIEWED.sourceMainSha256&&REVIEWED_SPEC.pipeCaseNames.length===95&&REVIEWED_SPEC.expectedPipeChecks===5856,'REVIEWED_SOURCE_AND_SCOPE_REQUIRED');
}
export function reviewedProfile(native){
 const role=Object.keys(REVIEWED.profiles).find(key=>{const row=REVIEWED.profiles[key];return row.mode===native.mode&&row.mutation===(native.mutation??null);});
 need(role,'ORIGINAL_COMPILED_POLARITY_REQUIRED');return {role,value:REVIEWED.profiles[role]};
}
export function assertReviewedNative(native){
 reviewIdentity();const{value}=reviewedProfile(native);
 need(native.compileSucceeded===true&&native.status==='BUILT'&&native.sources?.find(r=>r.path==='native/main.cpp')?.sha256===REVIEWED.sourceMainSha256,'EXACT_REVIEWED_SOURCE_REQUIRED');
 need(equal(native.sources.map(({path:p,sha256})=>({path:p,sha256})),value.sources),'COMPLETE_NATIVE_SOURCE_MEMBERSHIP_REQUIRED');
 need(equal(native.core,value.core),'ACTUAL_CORE_LEDGER_CHANGED');
 need(native.generated.some(r=>path.resolve(r.path)===path.resolve(native.exe)&&r.sha256===value.nativeExeSha256),'COMPILED_EXECUTABLE_IDENTITY_REQUIRED');
}
export function verifyReviewedShape(result,{positive=true,exactChecks=true}={}){
 reviewIdentity();need(result?.fullTranslationUnit===true&&result.normalProductMainExecuted===false&&result.realDeviceIo===false&&result.forbiddenIo===0,'CONTROLLED_NATIVE_BOUNDARY');
 need(equal(result.cases?.map(r=>r.name),REVIEWED_SPEC.pipeCaseNames),'EXACT95_CASE_ORDER_REQUIRED');
 need(result.checks===REVIEWED_SPEC.expectedPipeChecks,'EXACT5856_CHECKS_REQUIRED');
 need(Array.isArray(result.assertionTrace)&&result.assertionTrace.length===result.checks&&result.assertionTrace.every((r,i)=>r.checkIndex===i+1&&typeof r.passed==='boolean'),'ALL_EXECUTED_TRACE_STATES_REQUIRED');
 need(equal(result.assertionTrace.map(({case:c,assertion})=>({case:c,assertion})),REVIEWED_SPEC.positiveAssertionSequence),'EXACT_ASSERTION_ORDER_AND_MULTIPLICITY_REQUIRED');
 const distribution=result.cases.map(r=>({name:r.name,checks:result.assertionTrace.filter(t=>t.case===r.name).length}));need(equal(distribution,REVIEWED_SPEC.perCaseCheckDistribution),'PER_CASE_DISTRIBUTION_REQUIRED');
 need(equal(pair(result.failures??[]),pair(result.assertionTrace.filter(r=>!r.passed))),'FAILURE_LIST_TRACE_DISAGREE');
 if(positive)need(result.status==='PASS'&&result.failures.length===0&&result.assertionTrace.every(r=>r.passed),'POSITIVE_BUSINESS_FAILURE');
}
export function verifyReviewedDisposeBranches(result){
 for(const expected of REVIEWED.branchCases){const row=result.cases.find(r=>r.name===expected.name),waits=row?.playerLockWaits;need(waits?.length===6&&equal(waits,expected.waits),'REVIEWED_SIX_WAITS_AND_CLOCKS_REQUIRED:'+expected.name);
  const last=waits[5],request=last.before.requestId;need(request===6&&last.before.locked&&last.before.phase==='verifying'&&last.before.candidateOutcome==='failure'&&last.before.verificationRemainingMs===10000,'REQUEST6_PROVISIONAL_CANDIDATE_REQUIRED');
  need(last.after.phase==='success'&&!last.after.locked&&last.after.elapsedMs===10000&&!last.after.verificationPending&&equal(last.afterVerification,last.after),'REQUEST6_ACTUAL10S_SUCCESS_REQUIRED');
  const rejects=row.lockedPlayerRequests.filter(r=>r.protection.requestId===request);need(equal(rejects.map(r=>r.protection.elapsedMs),[0,9999,10000])&&rejects.every(r=>r.sideEffectsUnchanged===true&&r.protection.locked&&r.protection.remainingMs===30000-r.protection.elapsedMs&&r.result.ok===false&&r.result.reason==='virtual-start-protection-active'),'REQUEST6_ORIGINAL_PLAYER_REJECTIONS_REQUIRED');
  const finals=row.nativeEvents.filter(e=>e.event==='input-host-switch-verification'&&e.detail.stage==='final'&&e.detail.requestId===request);need(finals.length===1,'REQUEST6_UNIQUE_FINAL_REQUIRED');const f=finals[0].detail;
  need(f.outcome==='success'&&f.persona==='disabled'&&f.elapsedMs===10000&&f.healthyChecks===3&&f.fallbackAttempts===0&&f.finalReason==='disabled-local-cleanup-verified'&&f.lastInspectionEvidence==='disabled-cleanup-proven'&&f.lifecycleVersion==='VC-LIFECYCLE-20261020-R3','REQUEST6_CURRENT_CLEANUP_PROOF_REQUIRED');
  for(const id of [1,2,3,5]){const wait=waits.find(r=>r.before.requestId===id);need(wait?.after.phase==='expired'&&wait.after.elapsedMs===30000&&wait.after.remainingMs===0,'OTHER_UNKNOWN_INFLIGHT30S_CAP_REQUIRED');}
  const child=row.restartManagedReceipt;need(child?.nativeObservedProcessExit===true&&child.nativeObservedExitCode===0&&child.finalCleanupOk===true&&child.hasController===false&&child.hasContext===false,'EXACT_REPLACEMENT_CHILD_RETURN_REQUIRED');
  const simulated=row.msiPhysicalReturnObservations?.find(r=>r.requestId===request&&r.where==='explicit-new-ON-clean-OFF-released-MSI-XInput');need(simulated?.oldHostExited===true&&simulated.managedInterfaceReleased===true&&simulated.qualified===true&&simulated.discharged===true&&simulated.dischargedDebtRows?.length===1&&simulated.dischargedDebtRows[0].basis==='xinput-readmitted','NOIO_SIMULATED_RETURN_CONSUMER_REQUIRED');
  for(const assertion of ['actual queued lifecycle work drains without stranding ownership','actual OFF verifier publishes exactly one expected final and no contradictory outcome','repeated actual Stop and verification cannot renew or contradict the unique OFF final','actual verification final or hard expiry only reopens admission and never creates a replacement process'])need(result.assertionTrace.some(r=>r.case===row.name&&r.assertion===assertion&&r.passed),'CURRENT_QUEUED_DUTY_AND_UNIQUE_FINAL_ASSERTION_REQUIRED');
 }
}
function verifyOwnershipReturn(result,rollback){
 for(const connected of [0,1]){const name='PS-RETURN/ROG/owned-HID-foreign-peer/xinput-mask-'+connected,row=result.cases.find(r=>r.name===name),w=row?.ownedReturnWitness;need(w?.verdict==='foreign-peer'&&w.connectedMask===connected&&w.sourceMask===1&&w.queryComplete===true&&w.queryError===0&&w.oldGeneration===1&&w.oldRoot.length>0&&w.psQueriesDuringConsumer===1,'FOREIGN_PS_RETURN_ACTUAL_WITNESS');
  need(w.ownedMatches===(rollback?1:0)&&w.restoreCallsAfter-w.restoreCallsBefore===(rollback?0:1),'EXACT_PROFILE_MARKER_OWNERSHIP_FAULT');
  need(w.stateBefore.quarantined&&w.stateBefore.prepared&&w.stateBefore.epoch>0&&w.stateBefore.runId.length>0,'EXACT_OLD_TUPLE_REQUIRED');
  if(rollback)need(w.stateAfter.quarantined&&w.stateAfter.prepared&&w.stateAfter.epoch===w.stateBefore.epoch&&w.stateAfter.runId===w.stateBefore.runId,'FAULT_RETAINS_FOREIGN_MISCLASSIFIED_TUPLE');
  else need(!w.stateAfter.quarantined&&!w.stateAfter.prepared&&!w.stateAfter.hostHandle&&w.stateAfter.epoch===0&&w.stateAfter.runId==='','POSITIVE_FOREIGN_PEER_DOES_NOT_HOLD_OLD_TUPLE');
  const c=row.managedReceipt;need(c.nativeObservedProcessExit&&c.nativeObservedExitCode===5&&c.finalCleanupOk&&!c.hasController&&!c.hasContext,'REAL_OWNED_CHILD_CLEANUP_BEFORE_CONTROLLED_PNP');
  const verdicts=row.nativeEvents.filter(e=>e.event==='input-host-ps-return-proof').map(e=>e.detail.verdict);need(equal(verdicts,rollback?['unknown','present','absent']:['unknown','absent']),'ACTUAL_READER_THEN_CONSUMER_VERDICTS_REQUIRED');
  const end=result.assertionTrace.find(r=>r.case===name&&r.assertion==='owned-PS return witness leaves no new target or retained local resources');need(end?.passed===true,'LATER_COMPLETE_ABSENCE_MUST_CLEANUP');
 }
}
function originalRecipe(text,name){const start=text.indexOf("if(mutation==='"+name+"'){"),end=text.indexOf('\nif(mutation===',start+1);need(start>=0&&end>start,'ORIGINAL_MUTATION_RECIPE_REQUIRED');return text.slice(start,end).replaceAll('\r\n','\n').trimEnd();}
export function reviewedExpectedFailures(key){need(REVIEWED.semantics[key],'KNOWN_CASE_REQUIRED');return REVIEWED.semantics[key].exactFailedInstances.map(({case:c,assertion})=>({case:c,assertion}));}
export function verifyReviewedExecution(contract,result,terminal){
 const key=contract.spec.key,n=contract.native.value,expected=REVIEWED.semantics[key];need(expected,'KNOWN_FAULT_POLARITY_REQUIRED');assertReviewedNative(n);const profile=reviewedProfile(n).value;
 need(hash(contract.native.manifest)===profile.manifest.sha256&&contract.native.pin.sha256===profile.manifest.sha256,'ORIGINAL_MANIFEST_NOT_RESIGNED');
 need(terminal?.exitCode===expected.nativeExitCode&&!terminal.timedOut&&!terminal.launchError&&!terminal.signal&&!terminal.outputTruncated,'EXPECTED_COMPLETE_NATIVE_EXIT_REQUIRED');
 verifyReviewedShape(result,{positive:key==='positive'});need(result.status===expected.expectedStatus,'ORIGINAL_SEMANTIC_STATUS_REQUIRED');
 need(equal(pair(result.failures),pair(expected.exactFailedInstances)),'EXACT_NAMED_FAILURE_MULTISET_REQUIRED');const failed=new Set(expected.exactFailedInstances.map(r=>r.checkIndex));need(result.assertionTrace.every(r=>r.passed===!failed.has(r.checkIndex)),'EVERY_TRACE_PASS_AND_FAIL_STATE_REQUIRED');
 for(const wanted of [...expected.exactFailedInstances,...expected.originalExecutedPassingInstances]){const row=result.assertionTrace[wanted.checkIndex-1];need(row?.case===wanted.case&&row.assertion===wanted.assertion&&row.passed===!failed.has(wanted.checkIndex),'EXACT_ORIGINAL_INSTANCE_REQUIRED');}
 const sourceManifest=read(profile.sourceManifest.path);need(hash(profile.sourceManifest.path)===profile.sourceManifest.sha256&&hash(profile.inert.path)===profile.inert.sha256,'ACTUAL_COMPILED_MUTATION_IDENTITY_REQUIRED');
 const inert=fs.readFileSync(profile.inert.path,'utf8');for(const row of profile.core.filter(r=>r.expectedSemanticNegative)){const actual=extract(inert,row.symbol);need(actual.sha256===row.executedSha256,'ACTUAL_MUTATED_FUNCTION_IDENTITY_REQUIRED');}
 const recipeHash=REVIEWED.expectedRecipeLfSha256[n.mutation];if(recipeHash){const source=n.sources.find(r=>r.path==='tools/input_coordinator_lifecycle_selftest.mjs');need(digest(originalRecipe(fs.readFileSync(source.snapshot,'utf8'),n.mutation))===recipeHash,'UNCHANGED_ORIGINAL_RECIPE_REQUIRED');}
 if(key==='psOwnership')need(sourceManifest.substitutions.filter(r=>r.negativeControl).length===1&&sourceManifest.substitutions.find(r=>r.negativeControl).symbol==='psWakeProbeOwnEndpoint','ORIGINAL_SINGLE_METHOD_FAULT_REQUIRED');
 if(key==='cancellation'){
  need(expected.exactFailedInstances.length===18&&expected.originalExecutedPassingInstances.length===11,'ORIGINAL18_AND11_REQUIRED');
  const worker=extract(inert,'inputHostPerformPsWakeRearm').code;for(const token of ['admission-changed-before-rebuild','admission-changed-during-rebuild','queuedCycle!=g_psWakeRequestCycleId.load','queuedCycle!=g_virtualStartProtection.view'])need(worker.includes(token),'RETAINED_CANCELLATION_GUARD_REQUIRED');
  for(const kind of ['off','exit','power','generation','persona','reentry']){const row=result.cases.find(r=>r.psWakeCancellation?.kind===kind),q=row?.psWakeCancellation;need(q?.callbackExecuted===true&&q.probeCalls===(kind==='generation'?2:1)&&q.markedRebuilt===false&&q.rearmRequested===false,'ORIGINAL_CANCELLATION_CALLBACK_PROBE_REQUIRED');const held=['generation','reentry'].includes(kind);need(q.recreateCommandsAdded===(held?0:1),'CANCELLATION_RECREATE_POLARITY_REQUIRED');need(row.nativeEvents.some(e=>e.event==='ps-wake-rearm-deferred'&&e.detail.reason==='admission-changed-before-rebuild'&&e.detail.queryRequestId===q.requestIdBefore&&e.detail.currentRequestId===q.requestIdAfter),'CANCELLATION_GUARD_OBSERVATION_REQUIRED');if(!held)need(result.failures.some(r=>r.case===row.name&&r.assertion==='actual RECREATE primitive refuses changed admission at send boundary'),'ORIGINAL_DESIGNATED_RECREATE_FAULT_REQUIRED');}
 }
 verifyReviewedDisposeBranches(result);verifyOwnershipReturn(result,key==='psOwnership');
 return {id:REVIEWED.id,sourceMainSha256:REVIEWED.sourceMainSha256,case:key,groups:95,checks:5856,originalRequest6ConditionalInstancesAccounted:18,exactNamedFailures:expected.exactFailedInstances.length,originalCancellationExecutedPass:expected.originalExecutedPassingInstances.length,NoIoSoftwareOnly:true,historicalRawRewritten:false};
}
